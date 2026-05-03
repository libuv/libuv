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

#include <stdlib.h>
#include <string.h>

static uv_udp2_t handle;
static int close_cb_called;

static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


TEST_IMPL(udp2_gso) {
  struct sockaddr_in addr;
  struct sockaddr_storage bound_addr;
  int bound_len;
  uv_udp2_mmsg_t msgs[2];
  uv_buf_t bufs[2];
  unsigned int max_seg;
  int r;

  ASSERT_OK(uv_udp2_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp2_bind(&handle, (const struct sockaddr*) &addr, 0));

  bound_len = sizeof(bound_addr);
  ASSERT_OK(uv_udp2_getsockname(&handle,
                                 (struct sockaddr*) &bound_addr,
                                 &bound_len));
  ASSERT_OK(uv_udp2_connect(&handle,
                              (const struct sockaddr*) &bound_addr));

  max_seg = uv_udp2_gso_max_segments(&handle);
  ASSERT(max_seg == 0 || max_seg == 64);

  bufs[0] = uv_buf_init("AAAA", 4);
  bufs[1] = uv_buf_init("BBBB", 4);
  memset(msgs, 0, sizeof(msgs));
  msgs[0].bufs = &bufs[0];
  msgs[0].nbufs = 1;
  msgs[1].bufs = &bufs[1];
  msgs[1].nbufs = 1;

  r = uv_udp2_try_send_batch(&handle, msgs, 2);
  ASSERT_EQ(2, r);

  uv_close((uv_handle_t*) &handle, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
