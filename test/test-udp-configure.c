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
 * Tests for uv_udp_configure(): enabling features post-bind via the explicit
 * configuration API instead of bind-time flags.
 */

static uv_udp_t sender;
static uv_udp_t receiver;

static int recv_cb_called;
static int send_cb_called;
static int close_cb_called;

static struct sockaddr_in receiver_addr;


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

  /* ECN was configured post-bind via uv_udp_configure; verify it works. */
  ASSERT_EQ(2, recv->ecn);  /* ECT(0) */

  /* pktinfo was configured post-bind; verify local address. */
  ASSERT_EQ(AF_INET, recv->local.ss_family);

  recv_cb_called++;

  uv_udp_recv_stop(handle);
  uv_close((uv_handle_t*) &receiver, close_cb);
  uv_close((uv_handle_t*) &sender, close_cb);
}


static void send_cb(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
  send_cb_called++;
}


/* Test: bind with no flags, then enable ECN + pktinfo via uv_udp_configure. */
TEST_IMPL(udp_configure) {
  struct sockaddr_in bind_addr;
  uv_udp_send_t send_req;
  uv_buf_t buf;
  int namelen;
  int r;

  recv_cb_called = 0;
  send_cb_called = 0;
  close_cb_called = 0;

  /* Init and bind receiver with NO flags. */
  r = uv_udp_init(uv_default_loop(), &receiver);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  r = uv_udp_bind(&receiver, (const struct sockaddr*) &bind_addr, 0);
  ASSERT_OK(r);

  /* Now configure ECN and pktinfo post-bind. */
  r = uv_udp_configure(&receiver, UV_UDP_RECVECN | UV_UDP_RECVPKTINFO);
  ASSERT_OK(r);

  /* Get the port the OS assigned. */
  namelen = sizeof(receiver_addr);
  r = uv_udp_getsockname(&receiver,
                           (struct sockaddr*) &receiver_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp_recv_start2(&receiver, alloc_cb, recv_cb);
  ASSERT_OK(r);

  /* Init sender. */
  r = uv_udp_init(uv_default_loop(), &sender);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  r = uv_udp_bind(&sender, (const struct sockaddr*) &bind_addr, 0);
  ASSERT_OK(r);

  r = uv_udp_set_ecn(&sender, 2);  /* ECT(0) */
  ASSERT_OK(r);

  /* Send a datagram. */
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

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* Test: uv_udp_configure returns UV_EBADF on an unbound handle. */
TEST_IMPL(udp_configure_ebadf) {
  uv_udp_t handle;
  int r;

  r = uv_udp_init(uv_default_loop(), &handle);
  ASSERT_OK(r);

  r = uv_udp_configure(&handle, UV_UDP_RECVECN);
  ASSERT_EQ(r, UV_EBADF);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* Test: uv_udp_configure with PMTUD post-bind. */
TEST_IMPL(udp_configure_pmtud) {
  struct sockaddr_in bind_addr;
  struct sockaddr_in connect_addr;
  uv_udp_t handle;
  uv_buf_t buf;
  size_t mtu;
  int namelen;
  int r;

  r = uv_udp_init(uv_default_loop(), &handle);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  r = uv_udp_bind(&handle, (const struct sockaddr*) &bind_addr, 0);
  ASSERT_OK(r);

  /* Enable PMTUD post-bind. */
  r = uv_udp_configure(&handle, UV_UDP_PMTUD);
  ASSERT_OK(r);

  /* Connect to self for try_send and MTU query. */
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

  /* MTU query should work after configure + connect. */
  r = uv_udp_getmtu(&handle, &mtu);
  if (r == 0)
    ASSERT_GE(mtu, 1280);
  else
    ASSERT_EQ(r, UV_ENOTSUP);

  uv_close((uv_handle_t*) &handle, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
