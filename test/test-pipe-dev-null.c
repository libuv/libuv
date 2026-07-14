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
#include "task.h"

/* The null device ("/dev/null" or "NUL") reports as UV_FILE and cannot be
 * registered with epoll on Linux, which used to abort the event loop. Make
 * sure both writing to and reading from it through a pipe behave: the write
 * completes cleanly (even with a trailing zero-length buffer, which used to
 * arm POLLOUT needlessly) and the read reports EOF.
 */

static int write_cb_called;
static int read_cb_called;
static int close_cb_called;
static char read_buf[64];


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);
  write_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  *buf = uv_buf_init(read_buf, sizeof(read_buf));
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  /* Reading from the null device yields immediate EOF. */
  ASSERT_EQ(nread, UV_EOF);
  read_cb_called++;
  uv_close((uv_handle_t*) stream, close_cb);
}


static uv_file open_dev_null(int flags) {
#ifdef _WIN32
  const char dev_null[] = "NUL";
#else
  const char dev_null[] = "/dev/null";
#endif
  uv_fs_t fs_req;
  uv_file fd;
  int r;

  r = uv_fs_open(NULL, &fs_req, dev_null, flags, 0, NULL);
  ASSERT_NE(r, -1);
  fd = (uv_file) fs_req.result;
  uv_fs_req_cleanup(&fs_req);
  return fd;
}


TEST_IMPL(pipe_write_dev_null) {
  uv_pipe_t out;
  uv_write_t req;
  uv_buf_t bufs[2];

#ifdef _WIN32
  RETURN_SKIP("uv_pipe_open() does not accept the NUL device on Windows");
#endif

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &out, 0));
  ASSERT_OK(uv_pipe_open(&out, open_dev_null(UV_FS_O_WRONLY)));

  bufs[0] = uv_buf_init("hello\n", 6);
  bufs[1] = uv_buf_init(NULL, 0);  /* trailing zero-length buffer */

  /* nbufs=2 used to raise SIGABRT on Linux. */
  ASSERT_OK(uv_write(&req, (uv_stream_t*) &out, bufs, 2, write_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, write_cb_called);

  uv_close((uv_handle_t*) &out, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_read_dev_null) {
  uv_pipe_t in;

#ifdef _WIN32
  RETURN_SKIP("uv_pipe_open() does not accept the NUL device on Windows");
#endif

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &in, 0));
  ASSERT_OK(uv_pipe_open(&in, open_dev_null(UV_FS_O_RDONLY)));

  ASSERT_OK(uv_read_start((uv_stream_t*) &in, alloc_cb, read_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, read_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
