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


static int connect_cb_called = 0;
static int close_cb_called = 0;



static void connect_cb(uv_connect_t* handle, int status) {
  ASSERT(handle != NULL);
  connect_cb_called++;
}



static void close_cb(uv_handle_t* handle) {
  ASSERT(handle != NULL);
  close_cb_called++;
}


TEST_IMPL(tcp_connect_error_fault) {
  const char garbage[] =
      "blah blah blah blah blah blah blah blah blah blah blah blah";
  const struct sockaddr_in* garbage_addr;
  uv_tcp_t server;
  int r;
  uv_connect_t req;

  garbage_addr = (const struct sockaddr_in*) &garbage;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT(r == 0);
  r = uv_tcp_connect(&req,
                     &server,
                     (const struct sockaddr*) garbage_addr,
                     connect_cb);
  ASSERT(r == UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(connect_cb_called == 0);
  ASSERT(close_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tcp_connect_after_bind) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  uv_tcp_t server2;
  int r;
  uv_connect_t req;

  ASSERT(0 == uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT(r == 0);
  r = uv_tcp_bind(&server, (struct sockaddr*) &addr, 0);
  ASSERT(r == 0);

  r = uv_tcp_init(uv_default_loop(), &server2);
  ASSERT(r == 0);
  r = uv_tcp_bind(&server2, (struct sockaddr*) &addr, 0);
  ASSERT(r == 0);

  r = uv_tcp_connect(&req,
                     &server,
                     (struct sockaddr*) &addr,
                     connect_cb);
  ASSERT(r == 0);

  r = uv_tcp_connect(&req,
                     &server2,
                     (struct sockaddr*) &addr,
                     connect_cb);
  ASSERT(r == UV_EADDRNOTAVAIL);


  uv_close((uv_handle_t*)&server, close_cb);
  uv_close((uv_handle_t*)&server2, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(connect_cb_called == 1);
  ASSERT(close_cb_called == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
