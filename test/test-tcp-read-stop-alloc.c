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

#include "uv.h"
#include "task.h"

enum { CLOSE, STOP };

static int alloc_cb_called;
static int read_cb_called;
static int close_or_stop;

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  switch (close_or_stop) {
    default:
      ASSERT(0 && "unreachable");
    case CLOSE:
      uv_close(handle, NULL);
      break;
    case STOP:
      ASSERT_EQ(0, uv_read_stop((uv_stream_t*) handle));
      break;
  }

  static char c;
  buf->base = &c;
  buf->len = 1;
  alloc_cb_called++;
}

static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  if (close_or_stop == STOP)
    uv_close((uv_handle_t*) handle, NULL);

  ASSERT_EQ(nread, 0);
  read_cb_called++;
}

static void write_cb(uv_write_t* req, int status) {
  ASSERT_EQ(0, status);
}

static void connect_cb(uv_connect_t* req, int status) {
  static uv_write_t write_req;
  uv_buf_t buf;

  buf.base = "OK";
  buf.len = 2;

  ASSERT_EQ(0, status);
  ASSERT_EQ(0, uv_read_start(req->handle, alloc_cb, read_cb));
  ASSERT_EQ(0, uv_write(&write_req, req->handle, &buf, 1, write_cb));
}

static int test(void) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_tcp_t tcp_handle;
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_EQ(0, uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_EQ(0, uv_tcp_init(loop, &tcp_handle));
  ASSERT_EQ(0, uv_tcp_connect(&connect_req,
                              &tcp_handle,
                              (const struct sockaddr*) &addr,
                              connect_cb));
  ASSERT_EQ(0, uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, alloc_cb_called);
  ASSERT_EQ(1, read_cb_called);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tcp_read_stop_from_alloc) {
  close_or_stop = STOP;
  return test();
}

TEST_IMPL(tcp_close_from_alloc) {
  close_or_stop = CLOSE;
  return test();
}
