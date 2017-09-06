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

#if !defined(_WIN32)
TEST_IMPL(pipe_open_read_pipe) {
  RETURN_SKIP("Test only for Windows.");
}
#elif !defined(UV_USE_PIPE_INTERRUPTER)
TEST_IMPL(pipe_open_read_pipe) {
  RETURN_SKIP("Test only avaiable with UV_USE_PIPE_INTERRUPTER enabled.");
}
#else

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
}

void pipe_read_thread_proc(void* arg) {
  uv_pipe_t* pipe;
  pipe = arg;
  uv_read_start((uv_stream_t*) pipe, alloc_cb, read_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  FATAL("loop should not exit");
}

TEST_IMPL(pipe_open_read_pipe) {
  int r, pipe_fd;
  uv_thread_t pipe_read_thread;
  uv_pipe_t uv_pipe, uv_reopen_pipe;
  uv_loop_t test_loop;
  HANDLE stdin_read_pipe, stdin_write_pipe;
  SECURITY_ATTRIBUTES sa_attr;

  sa_attr.nLength = sizeof(sa_attr);
  sa_attr.bInheritHandle = TRUE;
  sa_attr.lpSecurityDescriptor = NULL;
  r = CreatePipe(&stdin_read_pipe, &stdin_write_pipe, &sa_attr, 0);
  ASSERT(r != 0);

  r = uv_pipe_init(uv_default_loop(), &uv_pipe, 0);
  ASSERT(r == 0);
  pipe_fd = _open_osfhandle((intptr_t) stdin_read_pipe, 0);
  r = uv_pipe_open(&uv_pipe, pipe_fd);
  ASSERT(r == 0);

  r = uv_thread_create(&pipe_read_thread, pipe_read_thread_proc, &uv_pipe);
  ASSERT(r == 0);

  /* Give uv_run some time to start */
  uv_sleep(250);
  /* Try to access the pipe again, in different loop */  
  r = uv_loop_init(&test_loop);
  ASSERT(r == 0);
  r = uv_pipe_init(&test_loop, &uv_reopen_pipe, 0);
  ASSERT(r == 0);
  r = uv_pipe_open(&uv_reopen_pipe, pipe_fd);
  return TEST_OK;
}
#endif
