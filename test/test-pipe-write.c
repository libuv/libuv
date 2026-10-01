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
#include <stdlib.h>


#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif


static int write_cb_called;
static int close_cb_called;


#ifndef _WIN32
static uv_mutex_t malloc_mutex;
static int fail_malloc;


static void* fail_once_malloc(size_t size) {
  int fail;

  uv_mutex_lock(&malloc_mutex);
  fail = fail_malloc;
  fail_malloc = 0;
  uv_mutex_unlock(&malloc_mutex);

  return fail ? NULL : malloc(size);
}


static void rejected_write_cb(uv_write_t* req, int status) {
  ASSERT(0 && "rejected write must not call its callback");
}
#endif


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);
  write_cb_called++;
  uv_close((uv_handle_t*) req->handle, close_cb);
}


TEST_IMPL(pipe_write_trailing_empty_buf) {
#ifdef _WIN32
  RETURN_SKIP("Unix only test");
#else
  uv_pipe_t pipe_handle;
  uv_write_t write_req;
  uv_buf_t bufs[2];
  int fd;

  fd = open("/dev/null", O_WRONLY);
  ASSERT_GE(fd, 0);

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_handle, 0));
  ASSERT_OK(uv_pipe_open(&pipe_handle, fd));
  fd = -1; /* fd is owned by pipe_handle now. */

  bufs[0] = uv_buf_init("hello\n", 6);
  bufs[1] = uv_buf_init(NULL, 0);
  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &pipe_handle,
                     bufs,
                     ARRAY_SIZE(bufs),
                     write_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}


TEST_IMPL(pipe_write_oom) {
#ifdef _WIN32
  RETURN_SKIP("Unix only test");
#else
  uv_loop_t loop;
  uv_pipe_t pipe_handle;
  uv_write_t rejected_req;
  uv_write_t write_req;
  uv_buf_t bufs[5];
  uv_file fds[2];
  char received[10];
  unsigned int i;

  ASSERT_OK(uv_mutex_init(&malloc_mutex));
  ASSERT_OK(uv_replace_allocator(fail_once_malloc, realloc, calloc, free));
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE));
  ASSERT_OK(uv_pipe_init(&loop, &pipe_handle, 0));
  ASSERT_OK(uv_pipe_open(&pipe_handle, fds[1]));

  /* Exceed the write request's inline buffer array. */
  for (i = 0; i < ARRAY_SIZE(bufs); i++)
    bufs[i] = uv_buf_init("x", 1);

  uv_mutex_lock(&malloc_mutex);
  fail_malloc = 1;
  uv_mutex_unlock(&malloc_mutex);
  ASSERT_EQ(UV_ENOMEM, uv_write(&rejected_req,
                               (uv_stream_t*) &pipe_handle,
                               bufs,
                               ARRAY_SIZE(bufs),
                               rejected_write_cb));
  ASSERT_EQ(0, uv_stream_get_write_queue_size((uv_stream_t*) &pipe_handle));
  ASSERT_OK(uv_loop_alive(&loop));

  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &pipe_handle,
                     bufs,
                     ARRAY_SIZE(bufs),
                     write_cb));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(5, read(fds[0], received, sizeof(received)));
  ASSERT_OK(close(fds[0]));
  ASSERT_OK(uv_loop_close(&loop));
  ASSERT_OK(uv_replace_allocator(malloc, realloc, calloc, free));
  uv_mutex_destroy(&malloc_mutex);
  uv_library_shutdown();
  return 0;
#endif
}
