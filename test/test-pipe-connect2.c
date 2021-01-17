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


static int close_cb_called = 0;
static int connect_cb_called = 0;
static int connection_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void connect_cb(uv_connect_t* connect_req, int status) {
  printf("status %d\n", status);
  ASSERT(status == 0);
  uv_close((uv_handle_t*)connect_req->handle, close_cb);
  connect_cb_called++;
}

static void connection_cb(uv_stream_t* server, int status) {
  int rv;
  ASSERT(status == 0);
  rv = uv_accept(server, (uv_stream_t*)server->data);
  ASSERT(rv == 0);
  uv_close((uv_handle_t*)server->data, close_cb);
  uv_close((uv_handle_t*)server, close_cb);
  connection_cb_called++;
}

TEST_IMPL(pipe_connect_to_abstract) {
#if defined(__linux__)
  const char path[] = "\0uv_pipe_test_connect_abstract";
  uv_pipe_t server;
  uv_pipe_t server_conn;
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init(uv_default_loop(), &server, 0);
  ASSERT(r == 0);
  r = uv_pipe_init(uv_default_loop(), &server_conn, 0);
  ASSERT(r == 0);
  server.data = &server_conn;

  r = uv_pipe_init(uv_default_loop(), &client, 0);
  ASSERT(r == 0);

  /* no flags defined yet */
  r = uv_pipe_bind2(&server, path, sizeof(path)-1, 1);
  ASSERT(r == UV_EINVAL);


  r = uv_pipe_bind2(&server, path, sizeof(path)-1, 0);
  ASSERT(r == 0);
  uv_listen((uv_stream_t*)&server, 5, connection_cb);

  uv_pipe_connect2(&req, &client, path, sizeof(path)-1, 0, connect_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(close_cb_called == 3);
  ASSERT(connect_cb_called == 1);
  ASSERT(connection_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
#else
  MAKE_VALGRIND_HAPPY();
  return 0;
#endif
}
