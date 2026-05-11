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

static uv_udp_t sender;
static uv_udp_t receiver;

static int recv_cb_called;
static int send_cb_called;
static int close_cb_called;
static int expect_v6;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void recv_cb(uv_udp_t* handle, const uv_udp_recv_t* recv) {
  if (recv->nread == 0)
    return;

  ASSERT_GT(recv->nread, 0);
  ASSERT_NOT_NULL(recv->addr);
  ASSERT_EQ(4, recv->nread);
  ASSERT(!memcmp("PING", recv->buf->base, 4));

  /* Verify the local destination address was populated.
   * On some platforms (e.g. macOS), IPv6 pktinfo may not be available
   * (no IPV6_RECVPKTINFO), so local.ss_family may be 0. */
  if (expect_v6) {
    if (recv->local.ss_family != 0) {
      const struct sockaddr_in6* local6;
      ASSERT_EQ(AF_INET6, recv->local.ss_family);
      local6 = (const struct sockaddr_in6*) &recv->local;
      ASSERT(IN6_IS_ADDR_LOOPBACK(&local6->sin6_addr));
      ASSERT_GT(recv->ifindex, 0);
    }
  } else {
    const struct sockaddr_in* local4;
    ASSERT_EQ(AF_INET, recv->local.ss_family);
    local4 = (const struct sockaddr_in*) &recv->local;
    ASSERT_EQ(local4->sin_addr.s_addr, htonl(INADDR_LOOPBACK));
    ASSERT_GT(recv->ifindex, 0);
  }

  recv_cb_called++;

  uv_udp_recv_stop(handle);
  uv_close((uv_handle_t*) &receiver, close_cb);
  uv_close((uv_handle_t*) &sender, close_cb);
}


static void send_cb(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
  send_cb_called++;
}


static int do_pktinfo_test_v4(void) {
  struct sockaddr_in bind_addr;
  struct sockaddr_in receiver_addr;
  uv_udp_send_t send_req;
  uv_buf_t buf;
  int namelen;
  int r;

  recv_cb_called = 0;
  send_cb_called = 0;
  close_cb_called = 0;
  expect_v6 = 0;

  r = uv_udp_init(uv_default_loop(), &receiver);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  r = uv_udp_bind(&receiver,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP_RECVPKTINFO);
  ASSERT_OK(r);

  namelen = sizeof(receiver_addr);
  r = uv_udp_getsockname(&receiver,
                           (struct sockaddr*) &receiver_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp_recv_start2(&receiver, alloc_cb, recv_cb);
  ASSERT_OK(r);

  r = uv_udp_init(uv_default_loop(), &sender);
  ASSERT_OK(r);

  buf = uv_buf_init("PING", 4);
  r = uv_udp_send(&send_req,
                    &sender,
                    &buf,
                    1,
                    (const struct sockaddr*) &receiver_addr,
                    send_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, recv_cb_called);
  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(2, close_cb_called);

  return 0;
}


static int do_pktinfo_test_v6(void) {
  struct sockaddr_in6 bind_addr;
  struct sockaddr_in6 receiver_addr;
  uv_udp_send_t send_req;
  uv_buf_t buf;
  int namelen;
  int r;

  recv_cb_called = 0;
  send_cb_called = 0;
  close_cb_called = 0;
  expect_v6 = 1;

  r = uv_udp_init(uv_default_loop(), &receiver);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip6_addr("::1", 0, &bind_addr));
  r = uv_udp_bind(&receiver,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP_RECVPKTINFO | UV_UDP_IPV6ONLY);
  if (r == UV_ENOTSUP || r == UV_EAFNOSUPPORT) {
    /* IPv6 not available. */
    uv_close((uv_handle_t*) &receiver, NULL);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    return 0;
  }
  ASSERT_OK(r);

  namelen = sizeof(receiver_addr);
  r = uv_udp_getsockname(&receiver,
                           (struct sockaddr*) &receiver_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp_recv_start2(&receiver, alloc_cb, recv_cb);
  ASSERT_OK(r);

  r = uv_udp_init(uv_default_loop(), &sender);
  ASSERT_OK(r);

  buf = uv_buf_init("PING", 4);
  r = uv_udp_send(&send_req,
                    &sender,
                    &buf,
                    1,
                    (const struct sockaddr*) &receiver_addr,
                    send_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, recv_cb_called);
  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(2, close_cb_called);

  return 0;
}


TEST_IMPL(udp_pktinfo) {
  ASSERT_OK(do_pktinfo_test_v4());
  ASSERT_OK(do_pktinfo_test_v6());

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
