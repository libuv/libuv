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


#ifdef _WIN32
# define BAD_PIPENAME "bad-pipe"
#else
# define BAD_PIPENAME "/path/to/unix/socket/that/really/should/not/be/there"
#endif


static int close_cb_called = 0;
static int connect_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void connect_cb(uv_connect_t* connect_req, int status) {
  ASSERT_EQ(status, UV_ENOENT);
  uv_close((uv_handle_t*) connect_req->handle, close_cb);
  connect_cb_called++;
}


static void connect_cb_file(uv_connect_t* connect_req, int status) {
  if (status != UV_ENOTSOCK)
    if (status != UV_EACCES)
      ASSERT_EQ(status, UV_ECONNREFUSED);
  uv_close((uv_handle_t*) connect_req->handle, close_cb);
  connect_cb_called++;
}


TEST_IMPL(pipe_connect_bad_name) {
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init(uv_default_loop(), &client, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &client, BAD_PIPENAME, connect_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_connect_to_file) {
  const char* path = "test/fixtures/empty_file";
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init(uv_default_loop(), &client, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &client, path, connect_cb_file);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#if !defined(_WIN32) && !defined(NO_SELF_CONNECT)
static uv_pipe_t test_server;
static uv_pipe_t test_server_conn;
static uv_pipe_t test_client;
static uv_connect_t test_req1;
static uv_connect_t test_req2;
static uv_connect_t test_req3;
static uv_write_t client_write_req;
static uv_write_t server_write_req;

static char server_recv_buf[16];
static size_t server_recv_len;
static char client_recv_buf[16];
static size_t client_recv_len;

static int first_connect_cb_called;
static int second_connect_cb_called;
static int rejected_connect_cb_called;
static int client_write_cb_called;
static int server_write_cb_called;
static int client_closed;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[64];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void rejected_connect_cb(uv_connect_t* req, int status) {
  rejected_connect_cb_called++;
}


static void in_progress_connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_pipe_init(server->loop, &test_server_conn, 0));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &test_server_conn));
  uv_close((uv_handle_t*) &test_server_conn, NULL);
}


static void in_progress_connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &test_req1);
  ASSERT_OK(status);
  connect_cb_called++;
  uv_close((uv_handle_t*) &test_client, close_cb);
  uv_close((uv_handle_t*) &test_server, close_cb);
}


static void maybe_close_client(void) {
  if (!client_closed && client_write_cb_called == 1 && client_recv_len == 4) {
    client_closed = 1;
    uv_close((uv_handle_t*) &test_client, close_cb);
    uv_close((uv_handle_t*) &test_server, close_cb);
  }
}


static void client_write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(req, &client_write_req);
  ASSERT_OK(status);
  client_write_cb_called++;
  maybe_close_client();
}


static void client_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  ASSERT_PTR_EQ(stream, (uv_stream_t*) &test_client);
  if (nread > 0) {
    ASSERT_LE(client_recv_len + (size_t) nread, sizeof(client_recv_buf));
    memcpy(client_recv_buf + client_recv_len, buf->base, (size_t) nread);
    client_recv_len += (size_t) nread;
    if (client_recv_len == 4) {
      ASSERT_MEM_EQ(client_recv_buf, "pong", 4);
      ASSERT_OK(uv_read_stop(stream));
      maybe_close_client();
    }
  } else if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);
  }
}


static void server_write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(req, &server_write_req);
  ASSERT_OK(status);
  server_write_cb_called++;
  uv_close((uv_handle_t*) req->handle, close_cb);
}


static void server_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  uv_buf_t pong;

  ASSERT_PTR_EQ(stream, (uv_stream_t*) &test_server_conn);
  if (nread > 0) {
    ASSERT_LE(server_recv_len + (size_t) nread, sizeof(server_recv_buf));
    memcpy(server_recv_buf + server_recv_len, buf->base, (size_t) nread);
    server_recv_len += (size_t) nread;
    if (server_recv_len == 4) {
      ASSERT_MEM_EQ(server_recv_buf, "ping", 4);
      ASSERT_OK(uv_read_stop(stream));
      pong = uv_buf_init("pong", 4);
      ASSERT_OK(uv_write(&server_write_req, stream, &pong, 1, server_write_cb));
    }
  } else if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);
  }
}


static void echo_connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_pipe_init(server->loop, &test_server_conn, 0));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &test_server_conn));
  ASSERT_OK(uv_read_start((uv_stream_t*) &test_server_conn,
                          alloc_cb,
                          server_read_cb));
}


static void start_client_io(uv_stream_t* stream) {
  uv_buf_t ping;

  ASSERT_EQ(1, uv_is_readable(stream));
  ASSERT_EQ(1, uv_is_writable(stream));
  ASSERT_OK(uv_read_start(stream, alloc_cb, client_read_cb));
  ping = uv_buf_init("ping", 4);
  ASSERT_OK(uv_write(&client_write_req, stream, &ping, 1, client_write_cb));
}


static void retry_second_connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &test_req2);
  ASSERT_OK(status);
  second_connect_cb_called++;
  start_client_io(req->handle);
}


static void retry_first_connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &test_req1);
  ASSERT_EQ(status, UV_ENOENT);
  ASSERT_EQ(0, uv_is_readable(req->handle));
  ASSERT_EQ(0, uv_is_writable(req->handle));
  first_connect_cb_called++;

  ASSERT_OK(uv_pipe_connect2(&test_req2,
                             (uv_pipe_t*) req->handle,
                             TEST_PIPENAME,
                             strlen(TEST_PIPENAME),
                             0,
                             retry_second_connect_cb));
}


static void bound_connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &test_req1);
  ASSERT_OK(status);
  connect_cb_called++;
  start_client_io(req->handle);
}
#endif /* !defined(_WIN32) && !defined(NO_SELF_CONNECT) */


TEST_IMPL(pipe_connect_in_progress) {
#if defined(_WIN32)
  RETURN_SKIP("Unix only test");
#elif defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#else
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();
  ASSERT_OK(uv_pipe_init(loop, &test_server, 0));
  ASSERT_OK(uv_pipe_bind(&test_server, TEST_PIPENAME));
  ASSERT_OK(uv_listen((uv_stream_t*) &test_server,
                      128,
                      in_progress_connection_cb));

  ASSERT_OK(uv_pipe_init(loop, &test_client, 0));

  r = uv_pipe_connect2(&test_req1,
                       &test_client,
                       TEST_PIPENAME,
                       strlen(TEST_PIPENAME),
                       0,
                       in_progress_connect_cb);
  ASSERT_OK(r);

  r = uv_pipe_connect2(&test_req2,
                       &test_client,
                       TEST_PIPENAME,
                       strlen(TEST_PIPENAME),
                       0,
                       rejected_connect_cb);
  ASSERT_EQ(r, UV_EALREADY);

  /* Invalid arguments still return UV_EINVAL before state checks. */
  r = uv_pipe_connect2(&test_req2,
                       &test_client,
                       NULL,
                       0,
                       0,
                       rejected_connect_cb);
  ASSERT_EQ(r, UV_EINVAL);

  /* Void wrapper must not overwrite the in-progress connect request. */
  uv_pipe_connect(&test_req3,
                  &test_client,
                  TEST_PIPENAME,
                  rejected_connect_cb);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(0, rejected_connect_cb_called);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}


TEST_IMPL(pipe_connect_retry_after_error) {
#if defined(_WIN32)
  RETURN_SKIP("Unix only test");
#elif defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#else
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();
  ASSERT_OK(uv_pipe_init(loop, &test_server, 0));
  ASSERT_OK(uv_pipe_bind(&test_server, TEST_PIPENAME));
  ASSERT_OK(uv_listen((uv_stream_t*) &test_server, 128, echo_connection_cb));

  ASSERT_OK(uv_pipe_init(loop, &test_client, 0));
  r = uv_pipe_connect2(&test_req1,
                       &test_client,
                       BAD_PIPENAME,
                       strlen(BAD_PIPENAME),
                       0,
                       retry_first_connect_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, first_connect_cb_called);
  ASSERT_EQ(1, second_connect_cb_called);
  ASSERT_EQ(1, client_write_cb_called);
  ASSERT_EQ(1, server_write_cb_called);
  ASSERT_EQ(4, server_recv_len);
  ASSERT_EQ(4, client_recv_len);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}


TEST_IMPL(pipe_connect_after_bind) {
#if defined(_WIN32)
  RETURN_SKIP("Unix only test");
#elif defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#else
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();
  ASSERT_OK(uv_pipe_init(loop, &test_server, 0));
  ASSERT_OK(uv_pipe_bind(&test_server, TEST_PIPENAME));
  ASSERT_OK(uv_listen((uv_stream_t*) &test_server, 128, echo_connection_cb));

  ASSERT_OK(uv_pipe_init(loop, &test_client, 0));
  ASSERT_OK(uv_pipe_bind(&test_client, TEST_PIPENAME_2));

  r = uv_pipe_connect2(&test_req1,
                       &test_client,
                       TEST_PIPENAME,
                       strlen(TEST_PIPENAME),
                       0,
                       bound_connect_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, client_write_cb_called);
  ASSERT_EQ(1, server_write_cb_called);
  ASSERT_EQ(4, server_recv_len);
  ASSERT_EQ(4, client_recv_len);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}
