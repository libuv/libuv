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
static uv_timer_t timer;
static int closed_streams;

static void close_cb(uv_handle_t *handle) {
  ++closed_streams;
}

static void write_cb_ok(uv_write_t* req, int status) {
  ASSERT_OK(status);
  ++write_complete;

  if (write_complete == 1) {
    uv_close((uv_handle_t*)&pipe_in, close_cb);
    uv_close((uv_handle_t*)&pipe_out, close_cb);
  }
}

static void read_cb(uv_stream_t* stream,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  ASSERT_GE(nread, 0);
  total_read += nread;
  free(buf->base);
  if (total_read == 12 + FILL_PIPE_NUM)
    uv_read_stop(stream);
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}

static void init_common(void) {
  loop = uv_default_loop();

  ASSERT_OK(uv_pipe_init(loop, &pipe_in, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_out, 0));
  ASSERT_OK(uv_pipe(fds, 0, 0));
  ASSERT_OK(uv_pipe_open(&pipe_out, fds[0]));
  ASSERT_OK(uv_pipe_open(&pipe_in, fds[1]));

  ASSERT_OK(uv_exepath(exepath, &exepath_size));
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = "spawn_helper2";
  args[2] = NULL;
  options.file = exepath;
  options.args = args;
  options.flags = 0;
  options.stdio_count = ARRAY_SIZE(stdios);
  options.stdio = stdios;
  stdios[0].flags = UV_IGNORE;
  stdios[1].flags = UV_INHERIT_STREAM;
  stdios[1].data.stream = (uv_stream_t*)&pipe_in;
  stdios[2].flags = UV_IGNORE;
}

/* After the subprocess exits, fill the pipe buffer. */
static void exit_cb_write(uv_process_t* process,
                          int64_t exit_status,
                          int term_signal) {
  ASSERT_EQ(1, exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*) process, NULL);
  ASSERT_OK(uv_write(&req, (uv_stream_t*)&pipe_in, &buf, 1, write_cb_ok));
}

TEST_IMPL(pipe_blocking_subprocess) {
#ifdef _WIN32
  RETURN_SKIP("Unix only test");
#endif

  init_common();
  options.exit_cb = exit_cb_write;

  ASSERT_OK(uv_read_start((uv_stream_t*)&pipe_out, alloc_cb, read_cb));

  /* Write enough that the pipe buffer fills. */
  buf.len = FILL_PIPE_NUM;
  buf.base = malloc(buf.len);
  memset(buf.base, 'A', buf.len);

  /* The subprocess forces fds[0] into blocking mode and writes 12 bytes. */
  ASSERT_OK(uv_spawn(loop, &process, &options));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(write_complete, 1);
  ASSERT_EQ(total_read, buf.len + 12);
  ASSERT_EQ(closed_streams, 2);

  free(buf.base);

  return 0;
}

static void timer_cb(uv_timer_t *handle) {
  /* The write has begun by now.  We'll close the write side of the pipe first,
   * since closing the read side will trigger SIGPIPE. */
  uv_close((uv_handle_t*) &pipe_in, close_cb);
  uv_close((uv_handle_t*) &timer, NULL);
}

static void write_cb_cancel(uv_write_t* req, int status) {
  ASSERT(status == UV_ECANCELED || status == UV_EPIPE);
  ++write_complete;
  uv_close((uv_handle_t*) &pipe_out, close_cb);
}

/* After the subprocess exits, fill the pipe buffer and to cancel the write. */
static void exit_cb_cancel(uv_process_t* process,
                           int64_t exit_status,
                           int term_signal) {
  ASSERT_EQ(1, exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*) process, NULL);
  ASSERT_OK(uv_write(&req, (uv_stream_t*)&pipe_in, &buf, 1, write_cb_cancel));
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 100, 0));
}

TEST_IMPL(pipe_blocking_cancel) {
#ifdef _WIN32
  RETURN_SKIP("Unix only test");
#endif

  init_common();
  options.exit_cb = exit_cb_cancel;

  uv_timer_init(loop, &timer);

  /* Write enough that the pipe buffer fills. */
  buf.len = FILL_PIPE_NUM;
  buf.base = malloc(buf.len);
  memset(buf.base, 'A', buf.len);

  /* The subprocess forces fds[0] into blocking mode and writes 12 bytes. */
  ASSERT_OK(uv_spawn(loop, &process, &options));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(write_complete, 1);
  ASSERT_EQ(closed_streams, 2);

  free(buf.base);

  return 0;
}
