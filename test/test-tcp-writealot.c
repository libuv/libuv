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

#include "../oio.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>


#define WRITES            3
#define CHUNKS_PER_WRITE  3
#define CHUNK_SIZE        10485760 /* 10 MB */

#define TOTAL_BYTES       (WRITES * CHUNKS_PER_WRITE * CHUNK_SIZE)


static char* send_buffer;


static int shutdown_cb_called = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;
static int bytes_sent = 0;
static int bytes_sent_done = 0;
static int bytes_received = 0;
static int bytes_received_done = 0;


static void close_cb(oio_handle* handle, int status) {
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  free(handle);

  close_cb_called++;
}


static void shutdown_cb(oio_req* req, int status) {
  ASSERT(req);
  ASSERT(status == 0);

  /* Now we wait for the EOF */
  shutdown_cb_called++;

  /* We should have had all the writes called already. */
  ASSERT(write_cb_called == WRITES);

  free(req);
}


static void read_cb(oio_handle* handle, int nread, oio_buf buf) {
  ASSERT(handle != NULL);

  if (nread < 0) {
    ASSERT(oio_last_error().code == OIO_EOF);
    printf("GOT EOF\n");

    if (buf.base) {
      free(buf.base);
    }

    oio_close(handle);
    return;
  }

  bytes_received_done += nread;

  free(buf.base);
}


static void write_cb(oio_req* req, int status) {
  ASSERT(req != NULL);

  if (status) {
    oio_err err = oio_last_error();
    fprintf(stderr, "oio_write error: %s\n", oio_strerror(err));
    ASSERT(0);
  }

  bytes_sent_done += CHUNKS_PER_WRITE * CHUNK_SIZE;
  write_cb_called++;

  free(req);
}


static void connect_cb(oio_req* req, int status) {
  oio_buf send_bufs[CHUNKS_PER_WRITE];
  oio_handle* handle;
  int i, j, r;

  ASSERT(req != NULL);
  ASSERT(status == 0);

  handle = req->handle;

  connect_cb_called++;
  free(req);

  /* Write a lot of data */
  for (i = 0; i < WRITES; i++) {
    for (j = 0; j < CHUNKS_PER_WRITE; j++) {
      send_bufs[j].len = CHUNK_SIZE;
      send_bufs[j].base = send_buffer + bytes_sent;
      bytes_sent += CHUNK_SIZE;
    }

    req = (oio_req*)malloc(sizeof *req);
    ASSERT(req != NULL);

    oio_req_init(req, handle, write_cb);
    r = oio_write(req, (oio_buf*)&send_bufs, CHUNKS_PER_WRITE);
    ASSERT(r == 0);
  }

  /* Shutdown on drain. FIXME: dealloc req? */
  req = (oio_req*) malloc(sizeof(oio_req));
  ASSERT(req != NULL);
  oio_req_init(req, handle, shutdown_cb);
  r = oio_shutdown(req);
  ASSERT(r == 0);

  /* Start reading */
  req = (oio_req*)malloc(sizeof *req);
  ASSERT(req != NULL);

  oio_req_init(req, handle, read_cb);
  r = oio_read_start(handle, read_cb);
  ASSERT(r == 0);
}


static oio_buf alloc_cb(oio_handle* handle, size_t size) {
  oio_buf buf;
  buf.base = (char*)malloc(size);
  buf.len = size;
  return buf;
}


TEST_IMPL(tcp_writealot) {
  struct sockaddr_in addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  oio_handle* client = (oio_handle*)malloc(sizeof *client);
  oio_req* connect_req = (oio_req*)malloc(sizeof *connect_req);
  int r;

  ASSERT(client != NULL);
  ASSERT(connect_req != NULL);

  send_buffer = (char*)malloc(TOTAL_BYTES + 1);

  ASSERT(send_buffer != NULL);

  oio_init(alloc_cb);

  r = oio_tcp_init(client, close_cb, NULL);
  ASSERT(r == 0);

  oio_req_init(connect_req, client, connect_cb);
  r = oio_connect(connect_req, (struct sockaddr*)&addr);
  ASSERT(r == 0);

  oio_run();

  ASSERT(shutdown_cb_called == 1);
  ASSERT(connect_cb_called == 1);
  ASSERT(write_cb_called == WRITES);
  ASSERT(close_cb_called == 1);
  ASSERT(bytes_sent == TOTAL_BYTES);
  ASSERT(bytes_sent_done == TOTAL_BYTES);
  ASSERT(bytes_received_done == TOTAL_BYTES);

  return 0;
}
