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

#ifdef _WIN32

#if !defined(__MINGW32__) && !defined(__MINGW64__)
#define UV_SUPPORTS_WIN_UDS
#endif

static int use_shutdown = 0;
static uv_shutdown_t shutdown_client;

static int close_cb_called = 0;
static int shutdown_cb_called = 0;
static int server_connect_cb_called = 0;
static int client_connect_cb_called = 0;

static uv_pipe_t pipe_server;
static uv_pipe_t pipe_client;
const char *pipe_test_data = "send test through win uds pipe";

static void alloc_cb(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
  buf->base = malloc(size);
  buf->len = size;
}

static void close_cb(uv_handle_t *handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}

static void shutdown_cb(uv_shutdown_t *req, int status) {
  ASSERT_NOT_NULL(req);
  uv_close((uv_handle_t *) req->handle, close_cb);
  shutdown_cb_called++;
}

static void after_write_cb(uv_write_t *req, int status) {
  ASSERT_OK(status);
  free(req->data);
  free(req);
}

static void client_connect_cb(uv_connect_t *connect_req, int status) {
  ASSERT_EQ(status, 0);
  client_connect_cb_called++;

  // Server connected, send test data.
  uv_buf_t bufs[1];
  bufs[0] = uv_buf_init(pipe_test_data, strlen(pipe_test_data));
  uv_write_t *req = malloc(sizeof(*req));
  req->data = NULL;
  uv_write(req, connect_req->handle, bufs, 1, after_write_cb);
}

static void read_cb(uv_stream_t *stream,
                    ssize_t nread,
                    const uv_buf_t *buf) {
  char read[256];

  // Ignore read error.
  if (nread < 0 || !buf)
    return;

  // Test if the buffer length equal.
  ASSERT_EQ(nread, strlen(pipe_test_data));

  memcpy(read, buf->base, nread);
  read[nread] = '\0';

  // Test if data equal.
  ASSERT_STR_EQ(read, pipe_test_data);

  if (use_shutdown) {
    uv_shutdown(&shutdown_client, (uv_stream_t *) &pipe_client, shutdown_cb);
  } else {
    uv_close((uv_handle_t *) &pipe_client, close_cb);
  }

  uv_close((uv_handle_t *) &pipe_server, close_cb);
}

static void server_connect_cb(uv_stream_t *handle, int status) {
  ASSERT_EQ(status, 0);
  server_connect_cb_called++;

  // Client accepted, start reading.
  uv_pipe_t *conn = malloc(sizeof(uv_pipe_t));
  ASSERT_OK(uv_pipe_init_ex(handle->loop, conn, UV_PIPE_INIT_WIN_UDS));
  ASSERT_OK(uv_accept(handle, (uv_stream_t*) conn));
  ASSERT_OK(uv_read_start((uv_stream_t*) conn, alloc_cb, read_cb));
}

int test_pipe_win_uds() {
#if defined(UV_SUPPORTS_WIN_UDS)
  int r;
  uv_fs_t fs;
  uv_connect_t req;
  size_t size = MAX_PATH;
  char path[MAX_PATH];

  // The windows UDS needs to be created on disk, create in temp dir.
  r = uv_os_tmpdir(path, &size);
  ASSERT_OK(r);
  strcat_s(path, MAX_PATH, "\\uv_pipe_win_uds");

  // Remove the existing file, the file must not exist before server bind.
  uv_fs_unlink(uv_default_loop(), &fs, path, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  // Bind server
  r = uv_pipe_init_ex(uv_default_loop(), &pipe_server, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  r = uv_pipe_bind(&pipe_server, path);
  ASSERT_OK(r);
  uv_listen((uv_stream_t *) &pipe_server, SOMAXCONN, server_connect_cb);
  uv_read_start((uv_stream_t *) &pipe_server, alloc_cb, read_cb);

  // Connect client to server
  r = uv_pipe_init_ex(uv_default_loop(), &pipe_client, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &pipe_client, path, client_connect_cb);

  // Run the loop
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  if (use_shutdown) {
    ASSERT_EQ(1, shutdown_cb_called);
  }

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(1, server_connect_cb_called);
  ASSERT_EQ(1, client_connect_cb_called);
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_win_uds) {
  use_shutdown = 0;
  return test_pipe_win_uds();
}


TEST_IMPL(pipe_win_uds_shutdown) {
  use_shutdown = 1;
  return test_pipe_win_uds();
}


static void bad_name_connect_cb(uv_connect_t *connect_req, int status) {
  ASSERT_EQ(status, UV_ENOENT);
}


TEST_IMPL(pipe_win_uds_bad_name) {
#if defined(UV_SUPPORTS_WIN_UDS)
  int r;
  uv_connect_t req;
  uv_pipe_t pipe_server_1;
  uv_pipe_t pipe_server_2;
  uv_pipe_t pipe_client_1;
  const char *path_1 = "not/exist/file/path";
  const char *path_2 = "test/fixtures/empty_file";

  // Bind server 1 which has a bad path
  r = uv_pipe_init_ex(uv_default_loop(), &pipe_server_1, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  r = uv_pipe_bind(&pipe_server_1, path_1);
  ASSERT_EQ(r, UV_EINVAL);

  // Bind server 2 which file exists
  r = uv_pipe_init_ex(uv_default_loop(), &pipe_server_2, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  r = uv_pipe_bind(&pipe_server_2, path_2);
  ASSERT_EQ(r, UV_EEXIST);

  // Connect client to server with bad name
  r = uv_pipe_init_ex(uv_default_loop(), &pipe_client_1, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &pipe_client_1, path_1, bad_name_connect_cb);

  // Run the loop
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif
