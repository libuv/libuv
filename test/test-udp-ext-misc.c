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

/*
 * Miscellaneous tests for the UDP extension APIs:
 * - negative / validation tests
 * - recv_start / recv_start2 switching
 * - uv_udp_set_cpu_affinity
 * - UV_UDP_TXTIME bind flag
 */


/* ---- Negative tests ---- */

/* GRO and GRO_RAW are mutually exclusive at bind time. */
TEST_IMPL(udp_ext_gro_gro_raw_exclusive) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));

  r = uv_udp_bind(&handle,
                    (const struct sockaddr*) &addr,
                    UV_UDP_GRO | UV_UDP_GRO_RAW);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* GRO and GRO_RAW are mutually exclusive via uv_udp_configure too. */
TEST_IMPL(udp_ext_configure_gro_exclusive) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&handle, (const struct sockaddr*) &addr, 0));

  r = uv_udp_configure(&handle, UV_UDP_GRO | UV_UDP_GRO_RAW);
  /* On Unix this returns UV_EINVAL; on Windows these flags are rejected. */
  ASSERT(r == UV_EINVAL);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* ECN value out of range. */
TEST_IMPL(udp_ext_ecn_out_of_range) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&handle, (const struct sockaddr*) &addr, 0));

  r = uv_udp_set_ecn(&handle, -1);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_udp_set_ecn(&handle, 4);
  ASSERT_EQ(r, UV_EINVAL);

  /* Valid range boundaries should succeed. */
  r = uv_udp_set_ecn(&handle, 0);
  ASSERT_OK(r);

  r = uv_udp_set_ecn(&handle, 3);
  /* CE (3) may be rejected on Windows. Accept both 0 and UV_EINVAL. */
  ASSERT(r == 0 || r == UV_EINVAL);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* APIs on unbound handles should return UV_EBADF. */
TEST_IMPL(udp_ext_ebadf) {
  uv_udp_t handle;
  size_t mtu;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));

  r = uv_udp_set_ecn(&handle, 0);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_udp_set_pmtud(&handle, UV_UDP_PMTUD_PROBE);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_udp_getmtu(&handle, &mtu);
  /* On platforms without IP_MTU (macOS/BSD), returns UV_ENOTSUP regardless. */
  ASSERT(r == UV_EBADF || r == UV_ENOTSUP);

  r = uv_udp_configure(&handle, UV_UDP_RECVECN);
  ASSERT_EQ(r, UV_EBADF);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* recv_start2 with NULL callbacks should fail. */
TEST_IMPL(udp_ext_recv_start2_null) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&handle, (const struct sockaddr*) &addr, 0));

  r = uv_udp_recv_start2(&handle, NULL, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* ---- recv_start / recv_start2 switching ---- */

static uv_udp_t sw_sender;
static uv_udp_t sw_receiver;
static int sw_recv1_called;
static int sw_recv2_called;
static int sw_close_cb_called;

static struct sockaddr_in sw_addr;


static void sw_close_cb(uv_handle_t* handle) {
  sw_close_cb_called++;
}


static void sw_alloc_cb(uv_handle_t* handle,
                        size_t suggested_size,
                        uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}


/* Legacy recv callback. */
static void sw_recv1_cb(uv_udp_t* handle,
                        ssize_t nread,
                        const uv_buf_t* buf,
                        const struct sockaddr* addr,
                        unsigned flags) {
  if (nread == 0)
    return;

  ASSERT_GT(nread, 0);
  ASSERT_NOT_NULL(addr);
  sw_recv1_called++;

  /* After receiving via recv_start, switch to recv_start2. */
  uv_udp_recv_stop(handle);
}


/* Enhanced recv2 callback. */
static void sw_recv2_cb(uv_udp_t* handle, const uv_udp_recv_t* recv) {
  if (recv->nread == 0)
    return;

  ASSERT_GT(recv->nread, 0);
  ASSERT_NOT_NULL(recv->addr);
  sw_recv2_called++;

  uv_udp_recv_stop(handle);
  uv_close((uv_handle_t*) &sw_receiver, sw_close_cb);
  uv_close((uv_handle_t*) &sw_sender, sw_close_cb);
}


static void sw_send_cb(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
}


/* Test switching from recv_start -> recv_stop -> recv_start2. */
TEST_IMPL(udp_ext_recv_switch) {
  struct sockaddr_in bind_addr;
  uv_udp_send_t send_req1;
  uv_udp_send_t send_req2;
  uv_buf_t buf;
  int namelen;
  int r;

  sw_recv1_called = 0;
  sw_recv2_called = 0;
  sw_close_cb_called = 0;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &sw_receiver));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  ASSERT_OK(uv_udp_bind(&sw_receiver,
                          (const struct sockaddr*) &bind_addr,
                          UV_UDP_RECVECN));

  namelen = sizeof(sw_addr);
  ASSERT_OK(uv_udp_getsockname(&sw_receiver,
                                 (struct sockaddr*) &sw_addr,
                                 &namelen));

  /* Start with the legacy recv path. */
  r = uv_udp_recv_start(&sw_receiver, sw_alloc_cb, sw_recv1_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_udp_init(uv_default_loop(), &sw_sender));

  /* Send first datagram to trigger legacy recv. */
  buf = uv_buf_init("MSG1", 4);
  r = uv_udp_send(&send_req1,
                    &sw_sender,
                    &buf,
                    1,
                    (const struct sockaddr*) &sw_addr,
                    sw_send_cb);
  ASSERT_OK(r);

  /* Run until the legacy callback stops. */
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, sw_recv1_called);
  ASSERT_EQ(0, sw_recv2_called);

  /* Now switch to recv_start2. */
  r = uv_udp_recv_start2(&sw_receiver, sw_alloc_cb, sw_recv2_cb);
  ASSERT_OK(r);

  /* Send second datagram to trigger recv2. */
  buf = uv_buf_init("MSG2", 4);
  r = uv_udp_send(&send_req2,
                    &sw_sender,
                    &buf,
                    1,
                    (const struct sockaddr*) &sw_addr,
                    sw_send_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, sw_recv1_called);
  ASSERT_EQ(1, sw_recv2_called);
  ASSERT_EQ(2, sw_close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* ---- uv_udp_set_cpu_affinity ---- */

TEST_IMPL(udp_ext_cpu_affinity) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&handle, (const struct sockaddr*) &addr, 0));

  r = uv_udp_set_cpu_affinity(&handle, 0);
  /* Linux 3.9+: should succeed. All others: UV_ENOTSUP. */
#if defined(__linux__)
  ASSERT_OK(r);
#else
  ASSERT_EQ(r, UV_ENOTSUP);
#endif

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* ---- UV_UDP_TXTIME ---- */

TEST_IMPL(udp_ext_txtime) {
  struct sockaddr_in addr;
  uv_udp_t handle;
  int r;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &handle));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));

  /* Binding with UV_UDP_TXTIME should succeed (or be silently ignored). */
  r = uv_udp_bind(&handle,
                    (const struct sockaddr*) &addr,
                    UV_UDP_TXTIME);
  ASSERT_OK(r);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
