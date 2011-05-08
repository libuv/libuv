/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * TODO: Add explanation of why we want on_close to be called from fresh
 * stack.
 */

#include "../oio.h"
#include "task.h"


static const char MESSAGE[] = "Failure is for the weak. Everyone dies alone.";

static oio_handle client;
static oio_req connect_req, write_req, timeout_req, shutdown_req;

static int nested = 0;
static int close_cb_called = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int timeout_cb_called = 0;
static int bytes_received = 0;
static int shutdown_cb_called = 0;


static void close_cb(oio_handle* handle, int status) {
  ASSERT(status == 0);
  ASSERT(nested == 0 && "close_cb must be called from a fresh stack");

  close_cb_called++;
}


static void shutdown_cb(oio_req* req, int status) {
  ASSERT(status == 0);
  ASSERT(nested == 0 && "shutdown_cb must be called from a fresh stack");

  shutdown_cb_called++;
}


static void read_cb(oio_handle* handle, int nread, oio_buf buf) {
  ASSERT(nested == 0 && "read_cb must be called from a fresh stack");

  printf("Read. nread == %d\n", nread);
  free(buf.base);

  if (nread == 0) {
    ASSERT(oio_last_error().code == OIO_EAGAIN);
    return;

  } else if (nread == -1) {
    ASSERT(oio_last_error().code == OIO_EOF);

    nested++;
    if (oio_close(handle)) {
      FATAL("oio_close failed");
    }
    nested--;

    return;
  }

  bytes_received += nread;

  /* We call shutdown here because when bytes_received == sizeof MESSAGE */
  /* there will be no more data sent nor received, so here it would be */
  /* possible for a backend to to call shutdown_cb immediately and *not* */
  /* from a fresh stack. */
  if (bytes_received == sizeof MESSAGE) {
    nested++;
    oio_req_init(&shutdown_req, handle, shutdown_cb);

    puts("Shutdown");

    if (oio_shutdown(&shutdown_req)) {
      FATAL("oio_shutdown failed");
    }
    nested--;
  }
}


static void timeout_cb(oio_req* req, int64_t skew, int status) {
  ASSERT(status == 0);
  ASSERT(nested == 0 && "timeout_cb must be called from a fresh stack");

  puts("Timeout complete. Now read data...");

  nested++;
  if (oio_read_start(&client, read_cb)) {
    FATAL("oio_read_start failed");
  }
  nested--;

  timeout_cb_called++;
}


static void write_cb(oio_req* req, int status) {
  ASSERT(status == 0);
  ASSERT(nested == 0 && "write_cb must be called from a fresh stack");

  puts("Data written. 500ms timeout...");

  /* After the data has been sent, we're going to wait for a while, then */
  /* start reading. This makes us certain that the message has been echoed */
  /* back to our receive buffer when we start reading. This maximizes the */
  /* tempation for the backend to use dirty stack for calling read_cb. */
  nested++;
  oio_req_init(&timeout_req, NULL, timeout_cb);
  if (oio_timeout(&timeout_req, 500)) {
    FATAL("oio_timeout failed");
  }
  nested--;

  write_cb_called++;
}


static void connect_cb(oio_req* req, int status) {
  oio_buf buf;

  puts("Connected. Write some data to echo server...");

  ASSERT(status == 0);
  ASSERT(nested == 0 && "connect_cb must be called from a fresh stack");

  nested++;

  buf.base = (char*) &MESSAGE;
  buf.len = sizeof MESSAGE;

  oio_req_init(&write_req, req->handle, write_cb);

  if (oio_write(&write_req, &buf, 1)) {
    FATAL("oio_write failed");
  }

  nested--;

  connect_cb_called++;
}


static oio_buf alloc_cb(oio_handle* handle, size_t size) {
  oio_buf buf;
  buf.len = size;
  buf.base = (char*) malloc(size);
  ASSERT(buf.base);
  return buf;
}


TEST_IMPL(callback_stack) {
  struct sockaddr_in addr = oio_ip4_addr("127.0.0.1", TEST_PORT);

  oio_init(alloc_cb);

  if (oio_tcp_init(&client, &close_cb, NULL)) {
    FATAL("oio_tcp_init failed");
  }

  puts("Connecting...");

  nested++;
  oio_req_init(&connect_req, &client, connect_cb);
  if (oio_connect(&connect_req, (struct sockaddr*) &addr)) {
    FATAL("oio_connect failed");
  }
  nested--;

  oio_run();

  ASSERT(nested == 0);
  ASSERT(connect_cb_called == 1 && "connect_cb must be called exactly once");
  ASSERT(write_cb_called == 1 && "write_cb must be called exactly once");
  ASSERT(timeout_cb_called == 1 && "timeout_cb must be called exactly once");
  ASSERT(bytes_received == sizeof MESSAGE);
  ASSERT(shutdown_cb_called == 1 && "shutdown_cb must be called exactly once");
  ASSERT(close_cb_called == 1 && "close_cb must be called exactly once");

  return 0;
}
