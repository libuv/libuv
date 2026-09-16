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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uv_udp_t handle;
static int close_cb_called;


static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


static int do_pmtud_test_v4(void) {
  struct sockaddr_in bind_addr;
  struct sockaddr_in connect_addr;
  uv_buf_t buf;
  size_t mtu;
  int namelen;
  int r;

  close_cb_called = 0;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));

  r = uv_udp_init(uv_default_loop(), &handle);
  ASSERT_OK(r);

  /* Bind with PMTUD enabled. */
  r = uv_udp_bind(&handle,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP_PMTUD);
  ASSERT_OK(r);

  /* Connect to ourselves for try_send. */
  namelen = sizeof(connect_addr);
  r = uv_udp_getsockname(&handle,
                           (struct sockaddr*) &connect_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp_connect(&handle, (const struct sockaddr*) &connect_addr);
  ASSERT_OK(r);

  /* Small send should always succeed. */
  buf = uv_buf_init("PING", 4);
  r = uv_udp_try_send(&handle, &buf, 1, NULL);
  ASSERT_EQ(4, r);

  /* Verify mode-switching works (each call should succeed or return
   * UV_ENOTSUP on platforms that don't distinguish modes). */
  r = uv_udp_set_pmtud(&handle, UV_UDP_PMTUD_OFF);
  ASSERT(r == 0 || r == UV_ENOTSUP);

  r = uv_udp_set_pmtud(&handle, UV_UDP_PMTUD_DO);
  ASSERT(r == 0 || r == UV_ENOTSUP);

  r = uv_udp_set_pmtud(&handle, UV_UDP_PMTUD_PROBE);
  ASSERT(r == 0 || r == UV_ENOTSUP);

  /* Query MTU. On Linux loopback this should be ~65535.
   * On macOS/BSD this returns UV_ENOTSUP. */
  r = uv_udp_getmtu(&handle, &mtu);
  if (r == 0) {
    /* Loopback MTU should be at least the IPv6 minimum (1280). */
    ASSERT_GE(mtu, 1280);
  } else {
    ASSERT_EQ(r, UV_ENOTSUP);
  }

  uv_close((uv_handle_t*) &handle, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);

  return 0;
}


static int do_pmtud_test_v6(void) {
  struct sockaddr_in6 bind_addr;
  struct sockaddr_in6 connect_addr;
  uv_buf_t buf;
  size_t mtu;
  int namelen;
  int r;

  close_cb_called = 0;

  ASSERT_OK(uv_ip6_addr("::1", 0, &bind_addr));

  r = uv_udp_init(uv_default_loop(), &handle);
  ASSERT_OK(r);

  /* Bind with PMTUD + IPV6ONLY. */
  r = uv_udp_bind(&handle,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP_PMTUD | UV_UDP_IPV6ONLY);
  if (r == UV_ENOTSUP || r == UV_EAFNOSUPPORT) {
    uv_close((uv_handle_t*) &handle, NULL);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    return 0;
  }
  ASSERT_OK(r);

  namelen = sizeof(connect_addr);
  r = uv_udp_getsockname(&handle,
                           (struct sockaddr*) &connect_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp_connect(&handle, (const struct sockaddr*) &connect_addr);
  ASSERT_OK(r);

  /* Small send should succeed. */
  buf = uv_buf_init("PING", 4);
  r = uv_udp_try_send(&handle, &buf, 1, NULL);
  ASSERT_EQ(4, r);

  /* Query MTU over IPv6. */
  r = uv_udp_getmtu(&handle, &mtu);
  if (r == 0)
    ASSERT_GE(mtu, 1280);

  uv_close((uv_handle_t*) &handle, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);

  return 0;
}


TEST_IMPL(udp_pmtud) {
  ASSERT_OK(do_pmtud_test_v4());
  ASSERT_OK(do_pmtud_test_v6());

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
