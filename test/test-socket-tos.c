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
#include <string.h>


TEST_IMPL(tcp_socket_set_get_tos) {
  uv_loop_t* loop;
  uv_tcp_t tcp;
  struct sockaddr_in addr;
  int r;
  int tos_in;
  int tos_out;

  loop = uv_default_loop();

  r = uv_tcp_init(loop, &tcp);
  ASSERT_OK(r);

  r = uv_ip4_addr("0.0.0.0", TEST_PORT, &addr);
  ASSERT_OK(r);

  r = uv_tcp_bind(&tcp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  /* Test valid TOS values */
  tos_in = 0;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 128;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 255;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  /* Test invalid TOS values */
  r = uv_socket_set_tos((uv_handle_t*) &tcp, -1);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_set_tos((uv_handle_t*) &tcp, 256);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_set_tos((uv_handle_t*) &tcp, 1000);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &tcp, NULL);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tcp6_socket_set_get_tos) {
  uv_loop_t* loop;
  uv_tcp_t tcp;
  struct sockaddr_in6 addr;
  int r;
  int tos_in;
  int tos_out;

  loop = uv_default_loop();

  r = uv_tcp_init(loop, &tcp);
  ASSERT_OK(r);

  r = uv_ip6_addr("::1", TEST_PORT, &addr);
  ASSERT_OK(r);

  r = uv_tcp_bind(&tcp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  /* Test valid TOS values */
  tos_in = 0;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 64;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 255;
  r = uv_socket_set_tos((uv_handle_t*) &tcp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  uv_close((uv_handle_t*) &tcp, NULL);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_socket_set_get_tos) {
  uv_loop_t* loop;
  uv_udp_t udp;
  struct sockaddr_in addr;
  int r;
  int tos_in;
  int tos_out;

  loop = uv_default_loop();

  r = uv_udp_init(loop, &udp);
  ASSERT_OK(r);

  r = uv_ip4_addr("0.0.0.0", TEST_PORT, &addr);
  ASSERT_OK(r);

  r = uv_udp_bind(&udp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  /* Test valid TOS values */
  tos_in = 0;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 32;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 255;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  /* Test invalid TOS values */
  r = uv_socket_set_tos((uv_handle_t*) &udp, -1);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_set_tos((uv_handle_t*) &udp, 256);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &udp, NULL);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp6_socket_set_get_tos) {
  uv_loop_t* loop;
  uv_udp_t udp;
  struct sockaddr_in6 addr;
  int r;
  int tos_in;
  int tos_out;

  loop = uv_default_loop();

  r = uv_udp_init(loop, &udp);
  ASSERT_OK(r);

  r = uv_ip6_addr("::1", TEST_PORT, &addr);
  ASSERT_OK(r);

  r = uv_udp_bind(&udp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  /* Test valid TOS values */
  tos_in = 0;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 96;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  tos_in = 255;
  r = uv_socket_set_tos((uv_handle_t*) &udp, tos_in);
  ASSERT_OK(r);

  r = uv_socket_get_tos((uv_handle_t*) &udp, &tos_out);
  ASSERT_OK(r);
  ASSERT_EQ(tos_out, tos_in);

  uv_close((uv_handle_t*) &udp, NULL);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(socket_tos_invalid_handle) {
  uv_loop_t* loop;
  uv_timer_t timer;
  uv_pipe_t pipe;
  int r;
  int tos;

  loop = uv_default_loop();

  /* Test with invalid handle types */
  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

  r = uv_socket_set_tos((uv_handle_t*) &timer, 64);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_get_tos((uv_handle_t*) &timer, &tos);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &timer, NULL);

  /* Test with pipe handle */
  r = uv_pipe_init(loop, &pipe, 0);
  ASSERT_OK(r);

  r = uv_socket_set_tos((uv_handle_t*) &pipe, 64);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_socket_get_tos((uv_handle_t*) &pipe, &tos);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*) &pipe, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(socket_tos_unbound_tcp) {
  uv_loop_t* loop;
  uv_tcp_t tcp;
  int r;
  int tos;

  loop = uv_default_loop();

  /* Test with unbound TCP socket - should fail since no address family */
  r = uv_tcp_init(loop, &tcp);
  ASSERT_OK(r);

  /* Getting/setting TOS on unbound socket should fail */
  r = uv_socket_set_tos((uv_handle_t*) &tcp, 64);
  ASSERT_NE(r, 0);

  r = uv_socket_get_tos((uv_handle_t*) &tcp, &tos);
  ASSERT_NE(r, 0);

  uv_close((uv_handle_t*) &tcp, NULL);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
