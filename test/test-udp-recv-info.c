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

#include <string.h>

#if !defined(_WIN32)
# include <netinet/in.h>
# include <sys/socket.h>
#endif

#define EXPECTED_TOS 0x62  /* AF41 + ECT(0) */

static uv_udp_t server;
static uv_udp_t client;
static uv_udp_send_t req;

static int recv_cb_called;
static int close_cb_called;


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


static void recv_cb(uv_udp_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf,
                    const struct sockaddr* addr,
                    unsigned flags) {
  uv_udp_recv_info_t info;
  const struct sockaddr_in* dst4;
  const struct sockaddr_in6* dst6;
  struct sockaddr_in6 expected6;
  struct sockaddr_in expected4;

  if (nread == 0 && addr == NULL)
    return;

  ASSERT_EQ(4, nread);
  ASSERT_OK(memcmp(buf->base, "PING", 4));

  ASSERT_OK(uv_udp_recv_info(handle, &info));

  ASSERT(info.valid & UV_UDP_RECV_TOS);
  ASSERT_EQ(EXPECTED_TOS, info.tos);

  ASSERT(info.valid & UV_UDP_RECV_TTL);
  ASSERT_GT(info.ttl, 0);
  ASSERT_LE(info.ttl, 255);

  ASSERT(info.valid & UV_UDP_RECV_PKTINFO);
  if (addr->sa_family == AF_INET) {
    ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &expected4));
    dst4 = (const struct sockaddr_in*) &info.dst;
    ASSERT_EQ(AF_INET, dst4->sin_family);
    ASSERT_OK(memcmp(&dst4->sin_addr,
                     &expected4.sin_addr,
                     sizeof(expected4.sin_addr)));
    ASSERT_GT(info.ifindex, 0);
  } else {
    ASSERT_OK(uv_ip6_addr("::1", 0, &expected6));
    dst6 = (const struct sockaddr_in6*) &info.dst;
    ASSERT_EQ(AF_INET6, dst6->sin6_family);
    ASSERT_OK(memcmp(&dst6->sin6_addr,
                     &expected6.sin6_addr,
                     sizeof(expected6.sin6_addr)));
    ASSERT_GT(info.ifindex, 0);
  }

  recv_cb_called++;
  uv_close((uv_handle_t*) &server, close_cb);
  uv_close((uv_handle_t*) &client, close_cb);
}


static void send_cb(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
}


#if !defined(_WIN32)
static void client_set_tos(uv_udp_t* handle, int family) {
  uv_os_fd_t fd;
  int tos;

  tos = EXPECTED_TOS;
  ASSERT_OK(uv_fileno((uv_handle_t*) handle, &fd));
  if (family == AF_INET6)
    ASSERT_OK(setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos)));
  else
    ASSERT_OK(setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)));
}
#endif


static int udp_recv_info_test(int family, unsigned int extra_flags) {
  struct sockaddr_in6 addr6;
  struct sockaddr_in addr4;
  const struct sockaddr* addr;
  struct sockaddr_in6 client_addr6;
  struct sockaddr_in client_addr4;
  const struct sockaddr* client_addr;
  uv_udp_recv_info_t info;
  uv_loop_t* loop;
  uv_buf_t buf;
  int r;

#if defined(__QEMU__)
  /* qemu-user does not reliably pass ancillary data through recvmsg. */
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  if (family == AF_INET6 && !can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  if (family == AF_INET6) {
    ASSERT_OK(uv_ip6_addr("::1", TEST_PORT, &addr6));
    ASSERT_OK(uv_ip6_addr("::1", 0, &client_addr6));
    addr = (const struct sockaddr*) &addr6;
    client_addr = (const struct sockaddr*) &client_addr6;
  } else {
    ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr4));
    ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &client_addr4));
    addr = (const struct sockaddr*) &addr4;
    client_addr = (const struct sockaddr*) &client_addr4;
  }

  loop = uv_default_loop();

  ASSERT_OK(uv_udp_init_ex(loop,
                           &server,
                           (family == AF_INET6 ? AF_INET6 : AF_INET) |
                               extra_flags));
  ASSERT_OK(uv_udp_bind(&server, addr, 0));

  /* Not inside a recv callback. */
  ASSERT_EQ(UV_EINVAL, uv_udp_recv_info(&server, &info));

  /* Unknown mask bits are rejected. */
  ASSERT_EQ(UV_EINVAL, uv_udp_set_recv_info(&server, 0x100));

  r = uv_udp_set_recv_info(&server,
                           UV_UDP_RECV_TOS |
                           UV_UDP_RECV_TTL |
                           UV_UDP_RECV_PKTINFO);
#if defined(_WIN32)
  ASSERT_EQ(UV_ENOTSUP, r);
  ASSERT_OK(uv_udp_set_recv_info(&server, 0));
  uv_close((uv_handle_t*) &server, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#else
  ASSERT_OK(r);

  ASSERT_OK(uv_udp_recv_start(&server, alloc_cb, recv_cb));

  ASSERT_OK(uv_udp_init(loop, &client));
  ASSERT_OK(uv_udp_bind(&client, client_addr, 0));
  client_set_tos(&client, family);

  buf = uv_buf_init("PING", 4);
  ASSERT_OK(uv_udp_send(&req, &client, &buf, 1, addr, send_cb));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, recv_cb_called);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}


TEST_IMPL(udp_recv_info) {
  return udp_recv_info_test(AF_INET, 0);
}


TEST_IMPL(udp_recv_info6) {
  return udp_recv_info_test(AF_INET6, 0);
}


TEST_IMPL(udp_recv_info_recvmmsg) {
  return udp_recv_info_test(AF_INET, UV_UDP_RECVMMSG);
}


TEST_IMPL(udp_recv_info_gro) {
  uv_loop_t* loop;
  uv_udp_t h;
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &addr, 0));

  r = uv_udp_set_recv_info(&h, UV_UDP_RECV_GRO);
#if defined(__linux__) && !defined(__QEMU__)
  /* UDP_GRO requires Linux 5.0+. */
  if (r == UV_ENOPROTOOPT)
    RETURN_SKIP("UDP_GRO not supported by this kernel");
  ASSERT_OK(r);
  ASSERT_OK(uv_udp_set_recv_info(&h, 0));
#elif !defined(__linux__)
  ASSERT_EQ(UV_ENOTSUP, r);
#endif

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
