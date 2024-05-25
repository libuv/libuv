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

static int connection_cb_called = 0;
static int do_accept_called = 0;
static int close_cb_called = 0;
static int connect_cb_called = 0;
uv_tcp_t* server;

static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  free(handle);
  close_cb_called++;
}

static void connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);

  if (connection_cb_called == 0) {
    ASSERT_OK(uv_reject(tcp));

#ifdef _WIN32
    ASSERT(server->tcp.serv.pending_accepts->accept_socket == INVALID_SOCKET);
#else
    ASSERT(server->accepted_fd == -1);
#endif
  } else {
    uv_tcp_t* accepted_handle = (uv_tcp_t*)malloc(sizeof *accepted_handle);

    ASSERT_NOT_NULL(accepted_handle);

    int r = uv_tcp_init(uv_default_loop(), accepted_handle);
    ASSERT_OK(r);

    r = uv_accept((uv_stream_t*)tcp, (uv_stream_t*)accepted_handle);
    ASSERT_OK(r);

    do_accept_called++;

    uv_close((uv_handle_t*)accepted_handle, close_cb);
  }

  /* After accepting the two clients close the server handle */
  if (do_accept_called == 1) {
    uv_close((uv_handle_t*)tcp, close_cb);
  }

  connection_cb_called++;
}

static void start_server(void) {
  struct sockaddr_in addr;
  server = (uv_tcp_t*)malloc(sizeof *server);
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_NOT_NULL(server);

  r = uv_tcp_init(uv_default_loop(), server);
  ASSERT_OK(r);
  r = uv_tcp_bind(server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)server, 128, connection_cb);
  ASSERT_OK(r);
}

static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);
  connect_cb_called++;
  free(req);
}

static void client_connect(uv_tcp_t* client) {
  struct sockaddr_in addr;
  int r;
  uv_connect_t* connect_req = malloc(sizeof *connect_req);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_NOT_NULL(client);
  ASSERT_NOT_NULL(connect_req);

  ASSERT_OK(uv_tcp_init(uv_default_loop(), client));

  r = uv_tcp_connect(connect_req,
                     client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);
}

TEST_IMPL(tcp_reject_accept) {
  uv_tcp_t client1;
  uv_tcp_t client2;

  start_server();

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &client1));
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &client2));

  client_connect(&client1);
  client_connect(&client2);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, connection_cb_called);
  ASSERT_EQ(1, do_accept_called);
  ASSERT_EQ(2, connect_cb_called);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
