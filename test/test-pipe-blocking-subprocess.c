/* Copyright libuv contributors. All rights reserved.
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

#define FILL_PIPE_NUM 0x40000

static uv_loop_t* loop;
static char exepath[1024];
static size_t exepath_size = sizeof exepath;
static char* args[3];
static uv_process_options_t options;
static uv_process_t process;
static uv_stdio_container_t stdios[3];
static uv_pipe_t pipe_in;
static uv_pipe_t pipe_out;
static uv_file fds[2];
static size_t total_read;
static int write_complete;
static uv_write_t req;
static uv_buf_t buf;

static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  ++write_complete;
}

static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  ASSERT_EQ(1, exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*) process, NULL);
  ASSERT_OK(uv_write(&req, (uv_stream_t*)&pipe_in, &buf, 1, write_cb));
}

static void read_cb(uv_stream_t* stream,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  ASSERT_GE(nread, 0);
  total_read += nread;
  free(buf->base);
  if (total_read == 12 + FILL_PIPE_NUM) {
    uv_read_stop(stream);
    uv_close((uv_handle_t*)&pipe_in, NULL);
    uv_close((uv_handle_t*)&pipe_out, NULL);
  }
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}

TEST_IMPL(pipe_blocking_subprocess) {

  loop = uv_default_loop();

  ASSERT_OK(uv_pipe_init(loop, &pipe_in, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_out, 0));
  ASSERT_OK(uv_pipe(fds, 0, 0));
  ASSERT_OK(uv_pipe_open(&pipe_out, fds[0]));
  ASSERT_OK(uv_pipe_open(&pipe_in, fds[1]));

  ASSERT_OK(uv_read_start((uv_stream_t*)&pipe_out, alloc_cb, read_cb));

  ASSERT_OK(uv_exepath(exepath, &exepath_size));
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = "spawn_helper2";
  args[2] = NULL;
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
  options.flags = 0;
  options.stdio_count = ARRAY_SIZE(stdios);
  options.stdio = stdios;
  stdios[0].flags = UV_IGNORE;
  stdios[1].flags = UV_INHERIT_STREAM;
  stdios[1].data.stream = (uv_stream_t*)&pipe_in;
  stdios[2].flags = UV_IGNORE;

  /* The subprocess forces fds[0] into blocking mode and writes 12 bytes. */
  ASSERT_OK(uv_spawn(loop, &process, &options));

  /* Now write enough that the pipe buffer fills. */
  buf.len = FILL_PIPE_NUM;
  buf.base = malloc(buf.len);
  memset(buf.base, 'A', buf.len);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(write_complete, 1);
  ASSERT_EQ(total_read, buf.len + 12);

  free(buf.base);

  return 0;
}
