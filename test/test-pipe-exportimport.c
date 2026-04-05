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

/*
 * Test that a connected pipe endpoint can be exported from one libuv loop
 * and imported into another.
 *
 * Sequence:
 *  1. Server thread: bind + listen on TEST_PIPENAME_2.
 *  2. Main thread waits at a barrier until the server is listening.
 *  3. Main thread connects a client; the client writes PIPE_DATA after
 *     the connection is established.
 *  4. Server accepts the connection, exports the accepted handle, closes
 *     its own copy, then signals the parent loop via an async handle.
 *  5. Parent loop imports the fd, starts reading, and verifies the payload
 *     sent by the client arrives intact.
 */

#include "uv.h"
#include "task.h"

#include <string.h>
#include <stdlib.h>

#define PIPE_DATA "pipe-exportimport-ok"

/* ── shared state ─────────────────────────────────────────────────── */
static uv_loop_t*  parent_loop;
static uv_loop_t*  server_loop;
static uv_thread_t server_thread;
static uv_barrier_t server_ready;   /* server → main: listening */
static uv_async_t  fd_ready;        /* server → parent: fd exported */
static uv_mutex_t  fd_mutex;
static int         exported_fd = -1;

static uv_pipe_t   server_handle;
static uv_pipe_t   server_conn;     /* accepted on server side, then exported */
static uv_pipe_t   parent_conn;     /* imported on parent side */
static uv_pipe_t   client_handle;
static uv_connect_t client_req;

static int  read_cb_called = 0;
static char read_buf[128];

/* ── callbacks ────────────────────────────────────────────────────── */

static void close_noop(uv_handle_t* h) { (void) h; }


static void alloc_cb(uv_handle_t* h,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  (void) h;
  (void) suggested_size;
  buf->base = read_buf;
  buf->len  = sizeof(read_buf);
}


static void read_cb(uv_stream_t* stream,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  (void) buf;
  if (nread == UV_EOF || nread == UV_ECONNRESET) {
    uv_close((uv_handle_t*) stream, close_noop);
    /* No more pending handles; the parent loop will exit. */
    uv_close((uv_handle_t*) &fd_ready, close_noop);
    return;
  }
  ASSERT_GT(nread, 0);
  ASSERT_EQ((size_t) nread, strlen(PIPE_DATA));
  ASSERT_OK(memcmp(read_buf, PIPE_DATA, (size_t) nread));
  read_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  free(req);
  /* Close the client so the server side sees EOF. */
  uv_close((uv_handle_t*) &client_handle, close_noop);
}


static void client_connect_cb(uv_connect_t* req, int status) {
  uv_write_t* wr;
  uv_buf_t    buf;

  ASSERT_OK(status);
  wr  = malloc(sizeof(*wr));
  ASSERT_NOT_NULL(wr);
  buf = uv_buf_init((char*) PIPE_DATA, strlen(PIPE_DATA));
  ASSERT_OK(uv_write(wr, req->handle, &buf, 1, write_cb));
}


static void on_fd_ready(uv_async_t* handle) {
  int fd;
  (void) handle;
  uv_mutex_lock(&fd_mutex);
  fd = exported_fd;
  uv_mutex_unlock(&fd_mutex);
  /* Import the exported connection into the parent loop and start reading. */
  ASSERT_OK(uv_pipe_import(parent_loop, fd, &parent_conn, 0));
  ASSERT_OK(uv_read_start((uv_stream_t*) &parent_conn, alloc_cb, read_cb));
}


static void on_connection(uv_stream_t* server, int status) {
  ASSERT_OK(status);

  ASSERT_OK(uv_pipe_init(server->loop, &server_conn, 0));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &server_conn));

  /* Export the accepted endpoint; the parent loop will take ownership. */
  {
    int fd;
    ASSERT_OK(uv_pipe_export(&server_conn, &fd));
    ASSERT_GT(fd, -1);
    uv_mutex_lock(&fd_mutex);
    exported_fd = fd;
    uv_mutex_unlock(&fd_mutex);
  }

  /* Surrender our copy so the imported handle is the sole owner. */
  uv_close((uv_handle_t*) &server_conn, close_noop);
  /* Stop accepting new connections. */
  uv_close((uv_handle_t*) server, close_noop);

  /* Signal the parent loop that the fd is ready. */
  ASSERT_OK(uv_async_send(&fd_ready));
}


/* ── server thread ────────────────────────────────────────────────── */

static void server_thread_entry(void* arg) {
  (void) arg;

  ASSERT_OK(uv_pipe_init(server_loop, &server_handle, 0));
  ASSERT_OK(uv_pipe_bind(&server_handle, TEST_PIPENAME_2));
  ASSERT_OK(uv_listen((uv_stream_t*) &server_handle, 1, on_connection));

  /* Unblock the main thread: the pipe name is registered and listening. */
  uv_barrier_wait(&server_ready);

  ASSERT_OK(uv_run(server_loop, UV_RUN_DEFAULT));
}


/* ── test entry ───────────────────────────────────────────────────── */

TEST_IMPL(pipe_exportimport) {
  parent_loop = uv_default_loop();
  server_loop = uv_loop_new();
  ASSERT_NOT_NULL(server_loop);

  ASSERT_OK(uv_barrier_init(&server_ready, 2));
  ASSERT_OK(uv_mutex_init(&fd_mutex));

  /* fd_ready lives on the parent loop. */
  ASSERT_OK(uv_async_init(parent_loop, &fd_ready, on_fd_ready));

  ASSERT_OK(uv_thread_create(&server_thread, server_thread_entry, NULL));

  /* Block until the server is listening before we attempt to connect. */
  uv_barrier_wait(&server_ready);
  uv_barrier_destroy(&server_ready);

  /* Connect a client; it will write PIPE_DATA once connected. */
  ASSERT_OK(uv_pipe_init(parent_loop, &client_handle, 0));
  uv_pipe_connect(&client_req, &client_handle, TEST_PIPENAME_2,
                  client_connect_cb);

  ASSERT_OK(uv_run(parent_loop, UV_RUN_DEFAULT));

  ASSERT_EQ(read_cb_called, 1);

  ASSERT_OK(uv_thread_join(&server_thread));

  uv_mutex_destroy(&fd_mutex);

  MAKE_VALGRIND_HAPPY(parent_loop);
  MAKE_VALGRIND_HAPPY(server_loop);

  return 0;
}
