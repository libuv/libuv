/* Copyright libuv project contributors. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uv.h"
#include "task.h"

#define REQ_COUNT 100

static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int close_cb_called;
static int write_cb_called;
static int cancelled_count;
static int connected;
static int closing;

static uv_write_t write_reqs[REQ_COUNT];
static char buf_data[16 * 1024];

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));
  connected++;
  ASSERT_EQ(1, connected);
}

static void write_cb(uv_write_t* req, int status) {
  write_cb_called++;
  if (status == UV_ECANCELED && !closing)
    cancelled_count++;

  if (cancelled_count >= 5 && !closing) {
    closing = 1;
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
    if (connected)
      uv_close((uv_handle_t*) &incoming, close_cb);
  }
}

static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  int r;
  int i;
  int cancel_count;

  ASSERT_OK(status);

  buf = uv_buf_init(buf_data, sizeof(buf_data));

  /* Queue many writes to fill the socket buffer */
  for (i = 0; i < REQ_COUNT; i++) {
    r = uv_write(&write_reqs[i],
                 req->handle,
                 &buf,
                 1,
                 write_cb);
    ASSERT_OK(r);
  }

  /* Cancel the trailing writes which should be queued */
  cancel_count = 0;
  for (i = REQ_COUNT - 5; i < REQ_COUNT; i++) {
    r = uv_write_cancel(&write_reqs[i]);
    ASSERT_OK(r);
    cancel_count++;
  }

  ASSERT_EQ(5, cancel_count);
}

TEST_IMPL(tcp_write_cancel) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int buffer_size;

  loop = uv_default_loop();

  close_cb_called = 0;
  write_cb_called = 0;
  cancelled_count = 0;
  connected = 0;
  closing = 0;
  buffer_size = sizeof(buf_data);
  memset(buf_data, 'A', sizeof(buf_data));

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, connection_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           connect_cb));
  /* Set the send buffer size small to ensure that writes get queued in
   * userspace rather than fitting entirely in the kernel's send buffer.
   * Also set the receive buffer small so the receiver doesn't drain data
   * and relieve backpressure. Note: Linux sets the actual buffer to twice
   * the requested size. */
  ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &client, &buffer_size));
  ASSERT_OK(uv_recv_buffer_size((uv_handle_t*) &client, &buffer_size));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  /* The writes we cancelled should have gotten UV_ECANCELED callbacks */
  ASSERT_EQ(5, cancelled_count);
  ASSERT_EQ(2 + connected, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/*
 * Test that uv_write_nwritten returns correct byte count on success.
 */
static uv_tcp_t nwritten_server;
static uv_tcp_t nwritten_client;
static uv_tcp_t nwritten_incoming;
static uv_write_t nwritten_req;
static int nwritten_cb_called;
static size_t nwritten_value;
static char nwritten_buf_data[1024];

static void nwritten_close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void nwritten_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  nwritten_cb_called++;
  nwritten_value = uv_write_nwritten(req);

  uv_close((uv_handle_t*) &nwritten_client, nwritten_close_cb);
  uv_close((uv_handle_t*) &nwritten_server, nwritten_close_cb);
  uv_close((uv_handle_t*) &nwritten_incoming, nwritten_close_cb);
}

static void nwritten_connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(tcp->loop, &nwritten_incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &nwritten_incoming));
}

static void nwritten_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;

  ASSERT_OK(status);

  buf = uv_buf_init(nwritten_buf_data, sizeof(nwritten_buf_data));
  ASSERT_OK(uv_write(&nwritten_req, req->handle, &buf, 1, nwritten_write_cb));
}

TEST_IMPL(tcp_write_nwritten) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;

  loop = uv_default_loop();

  close_cb_called = 0;
  nwritten_cb_called = 0;
  nwritten_value = 0;
  memset(nwritten_buf_data, 'B', sizeof(nwritten_buf_data));

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &nwritten_server));
  ASSERT_OK(uv_tcp_bind(&nwritten_server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &nwritten_server, 128, nwritten_connection_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &nwritten_client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &nwritten_client,
                           (struct sockaddr*) &addr,
                           nwritten_connect_cb));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, nwritten_cb_called);
  ASSERT_EQ(sizeof(nwritten_buf_data), nwritten_value);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/*
 * Test that uv_write_nwritten returns correct byte count for pipes.
 */
static uv_pipe_t pipe_client;
static uv_pipe_t pipe_server;
static uv_write_t pipe_write_req;
static int pipe_cb_called;
static size_t pipe_nwritten_value;
static char pipe_buf_data[1024];

static void pipe_close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void pipe_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  pipe_cb_called++;
  pipe_nwritten_value = uv_write_nwritten(req);

  uv_close((uv_handle_t*) &pipe_client, pipe_close_cb);
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
}

TEST_IMPL(pipe_write_nwritten) {
  uv_loop_t* loop;
  uv_buf_t buf;
  int fds[2];

  loop = uv_default_loop();

  close_cb_called = 0;
  pipe_cb_called = 0;
  pipe_nwritten_value = 0;
  memset(pipe_buf_data, 'C', sizeof(pipe_buf_data));

  ASSERT_OK(uv_pipe(fds, 0, 0));

  ASSERT_OK(uv_pipe_init(loop, &pipe_client, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_server, 0));

  ASSERT_OK(uv_pipe_open(&pipe_client, fds[1]));
  ASSERT_OK(uv_pipe_open(&pipe_server, fds[0]));

  buf = uv_buf_init(pipe_buf_data, sizeof(pipe_buf_data));
  ASSERT_OK(uv_write(&pipe_write_req,
                     (uv_stream_t*) &pipe_client,
                     &buf,
                     1,
                     pipe_write_cb));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, pipe_cb_called);
  ASSERT_EQ(sizeof(pipe_buf_data), pipe_nwritten_value);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
