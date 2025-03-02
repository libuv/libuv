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

#include "task.h"
#include "uv.h"

static uv_udp_t server;
static uv_udp_t client;


static void fail_cb(uv_udp_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf,
                    const struct sockaddr* addr,
                    unsigned flags) {
/* win will always call this cb so run the assertion in other platforms */
#ifndef _WIN32
  ASSERT(0 && "fail_cb called");
#endif
}


static void alloc_cb(uv_handle_t* handle, size_t sugg_size, uv_buf_t* buf) {
  ASSERT_OK(uv_udp_recv_stop((uv_udp_t*) handle));
}


TEST_IMPL(udp_alloc_cb_stop_recv) {
  struct sockaddr_in addr;
  uv_udp_send_t req;
  uv_buf_t buf;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_udp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_udp_recv_start(&server, alloc_cb, (uv_udp_recv_cb) fail_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  buf = uv_buf_init("PING", 4);
  r = uv_udp_send(&req,
                  &client,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  NULL);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
