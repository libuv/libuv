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

static int close_cb_called = 0;
static int connect_cb_called = 0;

static void close_cb(uv_handle_t *handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}

static void connect_cb_file(uv_connect_t *connect_req, int status) {
  ASSERT_EQ(status, 0);
  uv_close((uv_handle_t *) connect_req->handle, close_cb);
  connect_cb_called++;
}

TEST_IMPL(pipe_win_uds) {
  size_t size = MAX_PATH;
  char path[MAX_PATH];
  ASSERT_OK(uv_os_tmpdir(path, &size));
  strcat_s(path, MAX_PATH, "\\uv_pipe_win_uds");

  uv_fs_t fs;
  uv_fs_unlink(uv_default_loop(), &fs, path, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  uv_pipe_t server;
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init_ex(uv_default_loop(), &server, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  r = uv_pipe_bind(&server, path);
  ASSERT_OK(r);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  r = uv_pipe_init_ex(uv_default_loop(), &client, UV_PIPE_INIT_WIN_UDS);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &client, path, connect_cb_file);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif  /* _WIN32 */
