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

/* This benchmark spawns itself 1000 times. */

#include "task.h"
#include "uv.h"

static uv_loop_t* loop;

static int N = 1000;
static int done;

static uv_process_t process;
static uv_process_options_t options;
static char exepath[1024];
static size_t exepath_size;
static char* args[3];
static char* file;
static uv_pipe_t out;

#define OUTPUT_SIZE 1024
static char output[OUTPUT_SIZE];
static int output_used;

static int process_open;
static int pipe_open;


static void spawn(void);


static void maybe_spawn(void) {
  if (process_open == 0 && pipe_open == 0) {
    done++;
    if (done < N) {
      spawn();
    }
  }
}


static void process_close_cb(uv_handle_t* handle) {
  ASSERT_EQ(1, process_open);
  process_open = 0;
  maybe_spawn();
}


static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  ASSERT_EQ(42, exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*)process, process_close_cb);
}


static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = output + output_used;
  buf->len = OUTPUT_SIZE - output_used;
}


static void pipe_close_cb(uv_handle_t* pipe) {
  ASSERT_EQ(1, pipe_open);
  pipe_open = 0;
  maybe_spawn();
}


static void on_read(uv_stream_t* pipe, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    ASSERT_EQ(1, pipe_open);
    output_used += nread;
  } else if (nread < 0) {
    if (nread == UV_EOF) {
      uv_close((uv_handle_t*)pipe, pipe_close_cb);
    }
  }
}


static void spawn(void) {
  uv_stdio_container_t stdio[2];
  int r;

  ASSERT_OK(process_open);
  ASSERT_OK(pipe_open);

  args[0] = file;
  args[1] = "spawn_helper";
  args[2] = NULL;
  options.file = file;
  options.args = args;
  options.exit_cb = exit_cb;

  uv_pipe_init(loop, &out, 0);

  options.stdio = stdio;
  options.stdio_count = 2;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*)&out;

  r = uv_spawn(loop, &process, &options);
  ASSERT_OK(r);

  process_open = 1;
  pipe_open = 1;
  output_used = 0;

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);
}


static void setup_exepath(void) {
  int r;

  exepath_size = sizeof(exepath) - 1;
  r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);
  exepath[exepath_size] = '\0';
}


static int run_benchmark(const char* name) {
  int r;
  static int64_t start_time, end_time;

  loop = uv_default_loop();

  uv_update_time(loop);
  start_time = uv_now(loop);

  spawn();

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  uv_update_time(loop);
  end_time = uv_now(loop);

  fprintf(stderr, "%s: %.0f spawns/s\n",
          name,
          (double) N / (double) (end_time - start_time) * 1000.0);
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


BENCHMARK_IMPL(spawn) {
  setup_exepath();
  file = exepath;
  return run_benchmark("spawn");
}


BENCHMARK_IMPL(spawn_path) {
#ifdef _WIN32
  RETURN_SKIP("spawn_path is not supported on Windows.");
#else
  char path[4096];
  char* sep;
  size_t offset;
  int i;
  int r;

  setup_exepath();

  sep = strrchr(exepath, '/');
  ASSERT_NOT_NULL(sep);
  file = sep + 1;

  offset = 0;
  for (i = 0; i < 32; i++) {
    r = snprintf(path + offset,
                 sizeof(path) - offset,
                 "/nonexistent/libuv-spawn-path-%d:",
                 i);
    ASSERT_GT(r, 0);
    ASSERT_LT((size_t) r, sizeof(path) - offset);
    offset += r;
  }

  r = snprintf(path + offset,
               sizeof(path) - offset,
               "%.*s",
               (int) (sep - exepath),
               exepath);
  ASSERT_GT(r, 0);
  ASSERT_LT((size_t) r, sizeof(path) - offset);

  r = uv_os_setenv("PATH", path);
  ASSERT_OK(r);

  return run_benchmark("spawn_path");
#endif
}
