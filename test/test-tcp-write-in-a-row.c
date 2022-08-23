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

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int connect_cb_called;
static int close_cb_called;
static int connection_cb_called;
static int write_cb_called;
static char data[1024 * 1024 * 10]; // 10 MB, which is large than the send buffer size and the recv buffer

static void close_cb(uv_handle_t *handle)
{
  close_cb_called++;
}

static void write_cb(uv_write_t *w, int status)
{
  // the small write should finish immediately after the big write
  ASSERT(uv_stream_get_write_queue_size((uv_stream_t *)&client) == 0);

  write_cb_called++;

  if (write_cb_called == 2) {
    // we are done
    uv_close((uv_handle_t *)&client, close_cb);
    uv_close((uv_handle_t *)&incoming, close_cb);
    uv_close((uv_handle_t *)&server, close_cb);
  }
}

static void connect_cb(uv_connect_t *_, int status)
{
  int r;
  uv_buf_t buf;
  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  size_t write_queue_size0, write_queue_size1;

  ASSERT(status == 0);
  connect_cb_called++;

  // fire a big write
  buf = uv_buf_init(data, sizeof(data));
  r = uv_write(req, (uv_stream_t *)&client, &buf, 1, write_cb);
  ASSERT(r == 0);

  // check that the write process gets stuck
  write_queue_size0 = uv_stream_get_write_queue_size((uv_stream_t *)&client);
  ASSERT(write_queue_size0 > 0);

  // fire a small write, which should be queued
  buf = uv_buf_init("A", 1);
  req = (uv_write_t *)malloc(sizeof(uv_write_t));
  r = uv_write(req, (uv_stream_t *)&client, &buf, 1, write_cb);
  ASSERT(r == 0);

  write_queue_size1 = uv_stream_get_write_queue_size((uv_stream_t *)&client);
  ASSERT(write_queue_size1 == write_queue_size0 + 1);
}

static void alloc_cb(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
  static char base[1024];

  buf->base = base;
  buf->len = sizeof(base);
}

static void read_cb(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf) {}

static void connection_cb(uv_stream_t *tcp, int status)
{
  ASSERT(status == 0);
  connection_cb_called++;

  ASSERT(0 == uv_tcp_init(tcp->loop, &incoming));
  ASSERT(0 == uv_accept(tcp, (uv_stream_t *)&incoming));
  ASSERT(0 == uv_read_start((uv_stream_t *)&incoming, alloc_cb, read_cb));
}

static void start_server(void)
{
  struct sockaddr_in addr;

  ASSERT(0 == uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  ASSERT(0 == uv_tcp_init(uv_default_loop(), &server));
  ASSERT(0 == uv_tcp_bind(&server, (struct sockaddr *)&addr, 0));
  ASSERT(0 == uv_listen((uv_stream_t *)&server, 128, connection_cb));
}

TEST_IMPL(tcp_write_in_a_row)
{
#if defined(_WIN32)
  RETURN_SKIP("tcp_write_in_a_row does not work on Windows");
#endif

  uv_connect_t connect_req;
  struct sockaddr_in addr;

  start_server();

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT(0 == uv_tcp_init(uv_default_loop(), &client));
  ASSERT(0 == uv_tcp_connect(&connect_req,
                             &client,
                             (struct sockaddr *)&addr,
                             connect_cb));

  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT(connect_cb_called == 1);
  ASSERT(close_cb_called == 3);
  ASSERT(connection_cb_called == 1);
  ASSERT(write_cb_called == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
