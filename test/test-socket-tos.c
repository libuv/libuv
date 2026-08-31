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

static void setup_tcp(uv_loop_t* loop, uv_tcp_t* tcp, const char* ip,
                      int is_ip6) {
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
  int r;

  r = uv_tcp_init(loop, tcp);
  ASSERT_OK(r);

  if (is_ip6) {
    r = uv_ip6_addr(ip, TEST_PORT, &addr6);
    ASSERT_OK(r);
    r = uv_tcp_bind(tcp, (const struct sockaddr*)&addr6, 0);
  } else {
    r = uv_ip4_addr(ip, TEST_PORT, &addr4);
    ASSERT_OK(r);
    r = uv_tcp_bind(tcp, (const struct sockaddr*)&addr4, 0);
  }
  ASSERT_OK(r);
}

static void setup_udp(uv_loop_t* loop, uv_udp_t* udp, const char* ip,
                      int is_ip6) {
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
  int r;

  r = uv_udp_init(loop, udp);
  ASSERT_OK(r);

  if (is_ip6) {
    r = uv_ip6_addr(ip, TEST_PORT, &addr6);
    ASSERT_OK(r);
    r = uv_udp_bind(udp, (const struct sockaddr*)&addr6, 0);
  } else {
    r = uv_ip4_addr(ip, TEST_PORT, &addr4);
    ASSERT_OK(r);
    r = uv_udp_bind(udp, (const struct sockaddr*)&addr4, 0);
  }
  ASSERT_OK(r);
}

static void check_tos(uv_handle_t* handle, int tos) {
  int r;
  int tos_out;

  r = uv_socket_set_tos(handle, tos);
  ASSERT_OK(r);

  r = uv_socket_get_tos(handle, &tos_out);
  ASSERT_OK(r);
  /* Linux masks the TOS value. Sometimes (?) */
#if defined(__linux__)
  ASSERT((tos_out == tos) || tos_out == (tos & 0xFC));
#else
  ASSERT_EQ(tos_out, tos);
#endif
}

static void check_tos_fail(uv_handle_t* handle, int tos) {
  ASSERT_EQ(uv_socket_set_tos(handle, tos), UV_EINVAL);
}

TEST_IMPL(tcp_socket_set_get_tos) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS");
#endif
  uv_tcp_t tcp;

  setup_tcp(uv_default_loop(), &tcp, "0.0.0.0", 0);

  check_tos((uv_handle_t*)&tcp, 0);
  check_tos((uv_handle_t*)&tcp, 128);
  check_tos((uv_handle_t*)&tcp, 255);

  check_tos_fail((uv_handle_t*)&tcp, -1);
  check_tos_fail((uv_handle_t*)&tcp, 256);
  check_tos_fail((uv_handle_t*)&tcp, 1000);

  uv_close((uv_handle_t*)&tcp, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(tcp6_socket_set_get_tos) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS");
#endif
  uv_tcp_t tcp;

  setup_tcp(uv_default_loop(), &tcp, "::1", 1);

  check_tos((uv_handle_t*)&tcp, 0);
  check_tos((uv_handle_t*)&tcp, 64);
  check_tos((uv_handle_t*)&tcp, 255);

  uv_close((uv_handle_t *)&tcp, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(udp_socket_set_get_tos) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS");
#endif
  uv_udp_t udp;

  setup_udp(uv_default_loop(), &udp, "0.0.0.0", 0);

  check_tos((uv_handle_t*)&udp, 0);
  check_tos((uv_handle_t*)&udp, 32);
  check_tos((uv_handle_t*)&udp, 255);

  check_tos_fail((uv_handle_t *)&udp, -1);
  check_tos_fail((uv_handle_t *)&udp, 256);

  uv_close((uv_handle_t *)&udp, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(udp6_socket_set_get_tos) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS");
#endif
  uv_udp_t udp;

  setup_udp(uv_default_loop(), &udp, "::1", 1);

  check_tos((uv_handle_t*)&udp, 0);
  check_tos((uv_handle_t*)&udp, 96);
  check_tos((uv_handle_t*)&udp, 255);

  uv_close((uv_handle_t*)&udp, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(socket_tos_invalid_handle) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS");
#endif
  uv_timer_t timer;
  int r;
  int tos;

  /* Test with invalid handle types */
  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  r = uv_socket_set_tos((uv_handle_t*)&timer, 64);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_get_tos((uv_handle_t*)&timer, &tos);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&timer, NULL);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(socket_tos_unbound_tcp) {
#if defined(_WIN32)
  RETURN_SKIP("Windows does not support setting TOS on UDP sockets");
#endif
  uv_tcp_t tcp;
  int r;
  int tos;

  r = uv_tcp_init(uv_default_loop(), &tcp);
  ASSERT_OK(r);

  r = uv_socket_set_tos((uv_handle_t*)&tcp, 64);
  ASSERT_NE(r, 0);

  r = uv_socket_get_tos((uv_handle_t*)&tcp, &tos);
  ASSERT_NE(r, 0);

  uv_close((uv_handle_t*)&tcp, NULL);
  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
