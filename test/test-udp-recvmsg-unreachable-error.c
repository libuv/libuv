/* Copyright libuv project and contributors. All rights reserved.
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

#define CLIENT_TEST_PORT 9123
#define SERVER_TEST_PORT 9124
#define RECV_CB_MAX_CALL 3 /* ECONNREFUSED, EAGAIN/EWOULDBLOCK, ICMP delivery */

static int recv_cb_called = 0;
static int recverr_cb_called = 0;

static void udp_send_cb(uv_udp_send_t* req, int status) {
  /* Windows reports the ICMP error as ECONNRESET, and on the send when it is
   * the send that runs into it first. */
  ASSERT(status == 0 || status == UV_ECONNRESET);
}

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char storage[4]; /* "PING" */
  buf->base = storage;
  buf->len = sizeof(storage);
}

static void read_cb(uv_udp_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf,
                    const struct sockaddr* addr,
                    unsigned flags) {
  recv_cb_called++;
  /* Errors also reach us without UV_UDP_RECVERR: with IP_RECVERR set, a plain
   * recvmsg reports the pending socket error too. Only the errqueue delivery
   * carries the flag. */
  if (flags & UV_UDP_RECVERR) {
#ifdef _WIN32
    /* uv_translate_sys_error() maps the WSAECONNRESET the OS reports for the
     * ICMP error, so the exact value is libuv's to get right. */
    ASSERT_EQ(nread, UV_ECONNRESET);
#else
    /* Only the sign. ee_errno is numbered for the running kernel rather than
     * for the target libuv was built for, and the two disagree under
     * qemu-user: on MIPS the host sends 111, where the target's ECONNREFUSED
     * is 146 and 111 is not assigned at all. */
    ASSERT_LT(nread, 0);
#endif
    recverr_cb_called++;
  }
}

static void timer_cb(uv_timer_t* handle) {
  uv_udp_t* udp = handle->data;
  uv_close((uv_handle_t*) udp, NULL);
}

TEST_IMPL(udp_recvmsg_unreachable_error) {
#if !defined(__linux__) && !defined(_WIN32)
  RETURN_SKIP("ICMP error reporting is Linux- and Windows-specific");
#endif
  struct sockaddr_in server_addr, client_addr;
  uv_udp_t client;
  uv_timer_t timer;
  uv_udp_send_t send_req;
  uv_buf_t buf = uv_buf_init("PING", 4);
  ASSERT_OK(uv_ip4_addr("127.0.0.1", CLIENT_TEST_PORT, &client_addr));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", SERVER_TEST_PORT, &server_addr));
  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer));
  ASSERT_OK(uv_udp_bind(&client,
                        (const struct sockaddr*) &client_addr,
                        UV_UDP_RECVERR));
  ASSERT_OK(uv_udp_recv_start(&client, alloc_cb, read_cb));
  timer.data = &client;
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 3000, 0));
  ASSERT_OK(uv_udp_send(&send_req,
                        &client,
                        &buf,
                        1,
                        (const struct sockaddr*) &server_addr,
                        udp_send_cb));
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_GE(recverr_cb_called, 1);
#ifdef __linux__
  /* Windows makes no such promise: the zero-read drain loop delivers once
   * and re-queues. */
  ASSERT_EQ(recv_cb_called, RECV_CB_MAX_CALL);
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(udp_recvmsg_unreachable_error6) {
#if !defined(__linux__) && !defined(_WIN32)
  RETURN_SKIP("ICMP error reporting is Linux- and Windows-specific");
#endif
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  struct sockaddr_in6 server_addr, client_addr;
  uv_udp_t client;
  uv_timer_t timer;
  uv_udp_send_t send_req;
  uv_buf_t buf = uv_buf_init("PING", 4);

  ASSERT_OK(uv_ip6_addr("::1", CLIENT_TEST_PORT, &client_addr));
  ASSERT_OK(uv_ip6_addr("::1", SERVER_TEST_PORT, &server_addr));

  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer));

  ASSERT_OK(uv_udp_bind(&client,
                        (const struct sockaddr*) &client_addr,
                        UV_UDP_RECVERR));
  ASSERT_OK(uv_udp_recv_start(&client, alloc_cb, read_cb));

  timer.data = &client;
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 3000, 0));

  ASSERT_OK(uv_udp_send(&send_req,
                        &client,
                        &buf,
                        1,
                        (const struct sockaddr*) &server_addr,
                        udp_send_cb));

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_GE(recverr_cb_called, 1);
#ifdef __linux__
  /* Windows makes no such promise: the zero-read drain loop delivers once
   * and re-queues. */
  ASSERT_EQ(recv_cb_called, RECV_CB_MAX_CALL);
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
