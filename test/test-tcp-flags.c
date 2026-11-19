/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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


#ifndef _WIN32
#include <netinet/tcp.h>

static void check_same_option(uv_os_fd_t immediate,
                              uv_os_fd_t deferred,
                              int level,
                              int option) {
  int expected;
  int actual;
  socklen_t size;

  size = sizeof(expected);
  ASSERT_OK(getsockopt(immediate, level, option, &expected, &size));
  size = sizeof(actual);
  ASSERT_OK(getsockopt(deferred, level, option, &actual, &size));
  ASSERT_EQ(actual, expected);
}


/* Check explicit values, legacy defaults, and replacement of pending values. */
static void check_deferred_keepalive(uv_loop_t* loop, int family, int mode) {
  uv_tcp_t immediate;
  uv_tcp_t deferred;
  uv_tcp_t disabled;
  uv_tcp_t unopened;
  uv_os_fd_t immediate_fd;
  uv_os_fd_t deferred_fd;
  struct sockaddr_storage address;
  int length;
  int enabled;
  socklen_t size;

  ASSERT_OK(uv_tcp_init_ex(loop, &immediate, family));
  ASSERT_OK(uv_tcp_init(loop, &deferred));
  ASSERT_OK(uv_tcp_init(loop, &disabled));
  ASSERT_OK(uv_tcp_init(loop, &unopened));
  if (mode == 1) {
    ASSERT_OK(uv_tcp_keepalive(&immediate, 1, 30));
    ASSERT_OK(uv_tcp_keepalive(&deferred, 1, 30));
  } else if (mode == 0) {
    ASSERT_OK(uv_tcp_keepalive_ex(&immediate, 1, 60, 60, 60));
    ASSERT_OK(uv_tcp_keepalive_ex(&deferred, 1, 60, 60, 60));
  } else {
    ASSERT_OK(uv_tcp_keepalive_ex(&immediate, 1, 30, 20, 3));
    ASSERT_OK(uv_tcp_keepalive_ex(&deferred, 1, 60, 60, 60));
    ASSERT_OK(uv_tcp_keepalive_ex(&deferred, 0, 0, 0, 0));
    ASSERT_OK(uv_tcp_keepalive_ex(&deferred, 1, 30, 20, 3));
  }
  ASSERT_EQ(UV_EINVAL, uv_tcp_keepalive_ex(&deferred, 1, 0, 20, 3));
  ASSERT_EQ(UV_EINVAL, uv_tcp_keepalive_ex(&deferred, 1, 30, 0, 3));
  ASSERT_EQ(UV_EINVAL, uv_tcp_keepalive_ex(&deferred, 1, 30, 20, 0));
  ASSERT_EQ(UV_EBADF, uv_fileno((uv_handle_t*) &deferred, &deferred_fd));

  if (family == AF_INET)
    ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, (struct sockaddr_in*) &address));
  else
    ASSERT_OK(uv_ip6_addr("::1", 0, (struct sockaddr_in6*) &address));
  ASSERT_OK(uv_tcp_bind(&deferred, (const struct sockaddr*) &address, 0));
  length = sizeof(address);
  ASSERT_OK(uv_tcp_getsockname(&deferred, (struct sockaddr*) &address, &length));
  ASSERT_EQ(address.ss_family, family);
  ASSERT_OK(uv_fileno((uv_handle_t*) &immediate, &immediate_fd));
  ASSERT_OK(uv_fileno((uv_handle_t*) &deferred, &deferred_fd));
  check_same_option(immediate_fd, deferred_fd, SOL_SOCKET, SO_KEEPALIVE);
#ifdef TCP_KEEPIDLE
  check_same_option(immediate_fd, deferred_fd, IPPROTO_TCP, TCP_KEEPIDLE);
#elif defined(TCP_KEEPALIVE)
  check_same_option(immediate_fd, deferred_fd, IPPROTO_TCP, TCP_KEEPALIVE);
#endif
#ifdef TCP_KEEPINTVL
  check_same_option(immediate_fd, deferred_fd, IPPROTO_TCP, TCP_KEEPINTVL);
#endif
#ifdef TCP_KEEPCNT
  check_same_option(immediate_fd, deferred_fd, IPPROTO_TCP, TCP_KEEPCNT);
#endif

  /* Disabling pending settings must prevent replay at socket creation. */
  ASSERT_OK(uv_tcp_keepalive_ex(&disabled, 1, 30, 20, 3));
  ASSERT_OK(uv_tcp_keepalive_ex(&disabled, 0, 0, 0, 0));
  if (family == AF_INET)
    ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, (struct sockaddr_in*) &address));
  else
    ASSERT_OK(uv_ip6_addr("::1", 0, (struct sockaddr_in6*) &address));
  ASSERT_OK(uv_tcp_bind(&disabled, (const struct sockaddr*) &address, 0));
  ASSERT_OK(uv_fileno((uv_handle_t*) &disabled, &deferred_fd));
  size = sizeof(enabled);
  ASSERT_OK(getsockopt(deferred_fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, &size));
  ASSERT_EQ(enabled, 0);

  /* Closing pending settings must not require a socket or heap cleanup. */
  ASSERT_OK(uv_tcp_keepalive_ex(&unopened, 1, 30, 20, 3));
  ASSERT_EQ(UV_EBADF, uv_fileno((uv_handle_t*) &unopened, &deferred_fd));
  uv_close((uv_handle_t*) &immediate, NULL);
  uv_close((uv_handle_t*) &deferred, NULL);
  uv_close((uv_handle_t*) &disabled, NULL);
  uv_close((uv_handle_t*) &unopened, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
}
#endif


TEST_IMPL(tcp_flags) {
  uv_loop_t* loop;
  uv_tcp_t handle;
  int r;

  loop = uv_default_loop();

  /* Use _ex to make sure the socket is created. */
  r = uv_tcp_init_ex(loop, &handle, AF_INET);
  ASSERT_OK(r);

  r = uv_tcp_nodelay(&handle, 1);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 1, 60);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 0, 0);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 1, 0);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_tcp_keepalive_ex(&handle, 1, 60, 60, 60);
  ASSERT_OK(r);

  r = uv_tcp_keepalive_ex(&handle, 0, 0, 0, 0);
  ASSERT_OK(r);

  r = uv_tcp_keepalive_ex(&handle, 1, 0, 10, 10);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_tcp_keepalive_ex(&handle, 1, 10, 0, 10);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_tcp_keepalive_ex(&handle, 1, 10, 10, 0);
  ASSERT_EQ(r, UV_EINVAL);

#ifndef _WIN32
  check_deferred_keepalive(loop, AF_INET, 0);
  check_deferred_keepalive(loop, AF_INET, 1);
  check_deferred_keepalive(loop, AF_INET, 2);
  if (can_ipv6()) {
    check_deferred_keepalive(loop, AF_INET6, 0);
    check_deferred_keepalive(loop, AF_INET6, 1);
    check_deferred_keepalive(loop, AF_INET6, 2);
  } else {
    fprintf(stderr, "Deferred IPv6 keepalive checks unavailable.\n");
  }
#endif

  uv_close((uv_handle_t*)&handle, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
