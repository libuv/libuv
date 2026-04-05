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
#include "runner.h"
#include "task.h"

typedef struct {
  uv_loop_t* loop;
  uv_thread_t thread;
  uv_async_t* recv_channel;
  uv_async_t* send_channel;
  uv_tcp_t server;
  uv_tcp_t conn;
  int connection_accepted;
  int close_cb_called;
} worker_t;

static uv_async_t send_channel;
static uv_async_t recv_channel;
static worker_t parent;
static worker_t child;


typedef struct {
  uv_connect_t conn_req;
  uv_tcp_t conn;
} tcp_conn;

#define CONN_COUNT 100
static tcp_conn conns[CONN_COUNT];

static void close_cb(uv_handle_t* handle) {
  worker_t* worker = handle->data;
  ASSERT_NOT_NULL(worker);
  worker->close_cb_called++;
}


static void on_connection(uv_stream_t* server, int status) {
  worker_t* worker = container_of(server, worker_t, server);
  ASSERT_NOT_NULL(worker);
  ASSERT(worker == &parent || worker == &child);

  if (!worker->connection_accepted) {
    /* Accept the connection and close it. */
    ASSERT_OK(status);

    ASSERT_OK(uv_tcp_init(server->loop, &worker->conn));

    worker->conn.data = worker;

    ASSERT_OK(uv_accept(server, (uv_stream_t*)&worker->conn));

    worker->connection_accepted = 1;

    uv_close((uv_handle_t*)worker->recv_channel, close_cb);
    uv_close((uv_handle_t*)&worker->conn, close_cb);
    uv_close((uv_handle_t*)server, close_cb);
  }
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_close((uv_handle_t*)req->handle, NULL);
}


static void make_many_connections(void) {
  tcp_conn* conn;
  struct sockaddr_in addr;
  int i;

  for (i = 0; i < (int)ARRAY_SIZE(conns); i++) {
    conn = &conns[i];

    ASSERT_OK(uv_tcp_init(uv_default_loop(), &conn->conn));
    ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
    ASSERT_OK(uv_tcp_connect(&conn->conn_req,
                             (uv_tcp_t*)&conn->conn,
                             (struct sockaddr*) &addr,
                             connect_cb));
    conn->conn.data = conn;
  }
}


void on_parent_msg(uv_async_t* handle) {
  /* The fd was passed via handle->data by the child thread before async_send. */
  int fd = (int)(intptr_t) uv_handle_get_data((uv_handle_t*) handle);
  /* Restore data pointer so close_cb can reach the worker. */
  uv_handle_set_data((uv_handle_t*) handle, &parent);
  parent.server.data = &parent;

  /* Import the shared TCP server, and start listening on it. */
  ASSERT_OK(uv_tcp_import(parent.loop, fd, &parent.server, 0));

  ASSERT_OK(uv_listen((uv_stream_t*)&parent.server, 12, on_connection));
  ASSERT(parent.loop == parent.server.loop);

  /* Create a bunch of connections to get both servers to accept. */
  make_many_connections();
}


void on_child_msg(uv_async_t* handle) {
  ASSERT(!"no");
}


static void child_thread_entry(void* arg) {
  int listen_after_write = *(int*) arg;
  struct sockaddr_in addr;

  ASSERT_OK(uv_tcp_init(child.loop, &child.server));
  child.server.data = &child;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_bind(&child.server, (struct sockaddr*) &addr, 0));

  if (!listen_after_write)
    ASSERT_OK(uv_listen((uv_stream_t*)&child.server, 12, on_connection));

  {
    int fd;
    ASSERT_OK(uv_tcp_export(&child.server, &fd));
    ASSERT_GT(fd, -1);
    /* Pass fd to the parent loop callback via the async handle's data field.
     * uv_async_send provides the happens-before edge so no extra barrier needed. */
    uv_handle_set_data((uv_handle_t*) child.send_channel, (void*)(intptr_t) fd);
  }

  ASSERT_OK(uv_async_send(child.send_channel));

  if (listen_after_write)
    ASSERT_OK(uv_listen((uv_stream_t*)&child.server, 12, on_connection));

  ASSERT_OK(uv_run(child.loop, UV_RUN_DEFAULT));

  ASSERT(child.connection_accepted == 1);
  ASSERT(child.close_cb_called == 3);
}


static void run_tcp_exportimport_test(int listen_after_write) {
  parent.send_channel = &send_channel;
  parent.recv_channel = &recv_channel;
  child.send_channel = &recv_channel;
  child.recv_channel = &send_channel;

  parent.loop = uv_default_loop();
  child.loop = uv_loop_new();
  ASSERT(child.loop);

  ASSERT_OK(uv_async_init(parent.loop, parent.recv_channel, on_parent_msg));

  ASSERT_OK(uv_async_init(child.loop, child.recv_channel, on_child_msg));
  child.recv_channel->data = &child;

  ASSERT_OK(uv_thread_create(&child.thread,
                             child_thread_entry,
                             &listen_after_write));

  ASSERT_OK(uv_run(parent.loop, UV_RUN_DEFAULT));
  MAKE_VALGRIND_HAPPY(parent.loop);

  ASSERT_EQ(parent.connection_accepted, 1);
  ASSERT_EQ(parent.close_cb_called, 3);

  ASSERT_OK(uv_thread_join(&child.thread));

  MAKE_VALGRIND_HAPPY(child.loop);
}


TEST_IMPL(tcp_exportimport_listen_after_write) {
  run_tcp_exportimport_test(1);
  return 0;
}


TEST_IMPL(tcp_exportimport_listen_before_write) {
  run_tcp_exportimport_test(0);
  return 0;
}
