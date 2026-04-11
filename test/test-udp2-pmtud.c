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

static uv_udp2_t handle;
static int close_cb_called;


static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


TEST_IMPL(udp2_pmtud) {
  struct sockaddr_in bind_addr;
  struct sockaddr_in connect_addr;
  uv_buf_t buf;
  char* large_buf;
  size_t mtu;
  int namelen;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));

  r = uv_udp2_init(uv_default_loop(), &handle);
  ASSERT_OK(r);

  /* Bind with PMTUD enabled. */
  r = uv_udp2_bind(&handle,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP2_PMTUD);
  ASSERT_OK(r);

  /* Get the bound address so we can connect to ourselves. */
  namelen = sizeof(connect_addr);
  r = uv_udp2_getsockname(&handle,
                           (struct sockaddr*) &connect_addr,
                           &namelen);
  ASSERT_OK(r);

  /* Connect to loopback so we can use try_send without an address. */
  r = uv_udp2_connect(&handle, (const struct sockaddr*) &connect_addr);
  ASSERT_OK(r);

  buf = uv_buf_init("PING", 4);
  r = uv_udp2_try_send(&handle, &buf, 1, NULL);
  ASSERT_EQ(4, r);

  /* 65507 bytes = max UDP payload for IPv4. May fail with EMSGSIZE. */
  large_buf = calloc(1, 65507);
  ASSERT_NOT_NULL(large_buf);
  buf = uv_buf_init(large_buf, 65507);
  r = uv_udp2_try_send(&handle, &buf, 1, NULL);
  ASSERT(r == 65507 || r == UV_EMSGSIZE);
  free(large_buf);

  r = uv_udp2_set_pmtud(&handle, UV_UDP2_PMTUD_OFF);
  ASSERT_OK(r);

  r = uv_udp2_set_pmtud(&handle, UV_UDP2_PMTUD_DO);
  ASSERT_OK(r);

  r = uv_udp2_set_pmtud(&handle, UV_UDP2_PMTUD_PROBE);
  ASSERT_OK(r);

  /* May return UV_ENOTSUP on macOS/BSD. */
  r = uv_udp2_getmtu(&handle, &mtu);
  if (r == 0)
    ASSERT_GT(mtu, 0);

  uv_close((uv_handle_t*) &handle, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
