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

#include "task.h"
#include "uv.h"

typedef struct {
  uv_pipe_t pipe_handle;
  uv_connect_t conn_req;
} client_t;
static uv_pipe_t server_handle;
static int connect_cb_called;
static int do_accept_called = 0;
static int connection_cb_called = 0;
static uv_pipe_t connections[2];
static client_t clients[2];

static void connect_cb(uv_connect_t* _, int status) {
  ASSERT_OK(status);
  connect_cb_called++;
}

static void connection_pipe_cb(uv_stream_t* server, int status) {
  int r;
  uv_pipe_t* conn;

  conn = &connections[connection_cb_called];
  ASSERT_OK(status);

  if (connection_cb_called == 0) {
    ASSERT_OK(uv_reject(server));

    r = uv_pipe_init(server->loop, conn, 0);
    ASSERT_OK(r);

    r = uv_accept(server, (uv_stream_t*)conn);
    ASSERT(r == UV_EAGAIN);
  } else {
    r = uv_pipe_init(server->loop, conn, 0);
    ASSERT_OK(r);

    r = uv_accept(server, (uv_stream_t*)conn);
    ASSERT_OK(r);

    do_accept_called++;
  }

  /* After accepting the second client, close the server handle */
  if (do_accept_called == 1) {
    uv_close((uv_handle_t*) &server_handle, NULL);
  }

  connection_cb_called++;
}

TEST_IMPL(pipe_reject) {

  int r;
  uv_loop_t* loop;

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &server_handle, 0);
  ASSERT_OK(r);

  r = uv_pipe_bind(&server_handle, TEST_PIPENAME);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server_handle, 128, connection_pipe_cb);
  ASSERT_OK(r);

  r = uv_pipe_init(loop, &clients[0].pipe_handle, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&clients[0].conn_req,
                  &clients[0].pipe_handle,
                  TEST_PIPENAME,
                  connect_cb);

  r = uv_pipe_init(loop, &clients[1].pipe_handle, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&clients[1].conn_req,
                  &clients[1].pipe_handle,
                  TEST_PIPENAME,
                  connect_cb);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(2, connection_cb_called);
  ASSERT_EQ(2, connect_cb_called);
  ASSERT_EQ(1, do_accept_called);
  return 0;
}
