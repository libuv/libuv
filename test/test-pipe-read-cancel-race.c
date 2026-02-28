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

/* This test reproduces a Windows-specific race in the CancelIoEx path of
 * uv__pipe_read_data. When a child process writes chunks whose size exactly
 * matches the parent's read buffer size, every successful ReadFile fills the
 * entire buffer and libuv's read loop continues for another iteration. If the
 * pipe is momentarily empty, the next ReadFile returns ERROR_IO_PENDING and is
 * cancelled via CancelIoEx. There is a Windows kernel race where data arriving
 * concurrently with CancelIoEx can be drained from the pipe's internal buffer
 * to satisfy the pending read, then silently discarded when the cancellation
 * is applied. The read reports 0 bytes transferred, but the data is gone from
 * the pipe -- resulting in data loss.
 *
 * The repeated spawn + bulk transfer below is designed to hit this race window
 * many times. Without the fix it typically fails within a handful of attempts
 * on Windows; with the fix it should pass every attempt.
 */

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Chunk size for both the child's writes and the parent's read buffer. Any
 * size works as long as both sides agree -- the bug triggers when a ReadFile
 * returns exactly the requested byte count, regardless of the specific value.
 * Keep it larger than the 64 KiB default pipe buffer to make the race window
 * wider: each child write blocks partway through until the parent drains
 * the pipe, naturally interleaving writer and reader activity. */
#define CHUNK_SIZE   (70 * 1024)
#define NUM_CHUNKS   300
#define TOTAL_BYTES  ((size_t) CHUNK_SIZE * NUM_CHUNKS)
#define NUM_ATTEMPTS 3


static uv_process_t process;
static uv_pipe_t out_pipe;
static char* read_buf;
static size_t bytes_received;
static int attempt_failed;
static int process_exited;


static void close_cb(uv_handle_t* handle) {
}


static void exit_cb(uv_process_t* proc,
                    int64_t exit_status,
                    int term_signal) {
  ASSERT_OK(exit_status);
  ASSERT_OK(term_signal);
  process_exited = 1;
  uv_close((uv_handle_t*) proc, close_cb);
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  /* Return a buffer of exactly CHUNK_SIZE bytes. When the child writes
   * CHUNK_SIZE-byte chunks, ReadFile returns exactly buf->len, the read
   * loop continues, and the next iteration is likely to hit an empty pipe. */
  buf->base = read_buf;
  buf->len = CHUNK_SIZE;
}


static void read_cb(uv_stream_t* stream,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  if (nread > 0) {
    bytes_received += (size_t) nread;
  } else if (nread == UV_EOF) {
    uv_close((uv_handle_t*) stream, close_cb);
  } else if (nread < 0) {
    fprintf(stderr, "read_cb error: %s\n", uv_strerror((int) nread));
    attempt_failed = 1;
    uv_close((uv_handle_t*) stream, close_cb);
  }
  /* nread == 0 means EAGAIN / cancelled -- ignore, libuv will retry. */
}


static int run_one_attempt(uv_loop_t* loop) {
  uv_process_options_t options;
  uv_stdio_container_t stdio[3];
  char exepath[1024];
  size_t exepath_size;
  char* args[3];
  int r;

  bytes_received = 0;
  attempt_failed = 0;
  process_exited = 0;

  exepath_size = sizeof(exepath);
  r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);
  exepath[exepath_size] = '\0';

  args[0] = exepath;
  args[1] = "pipe_read_cancel_race_helper";
  args[2] = NULL;

  r = uv_pipe_init(loop, &out_pipe, 0);
  ASSERT_OK(r);

  memset(&options, 0, sizeof(options));
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
  options.stdio = stdio;
  options.stdio_count = 3;
  stdio[0].flags = UV_IGNORE;
  stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  stdio[1].data.stream = (uv_stream_t*) &out_pipe;
  stdio[2].flags = UV_INHERIT_FD;
  stdio[2].data.fd = 2;

  r = uv_spawn(loop, &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out_pipe, alloc_cb, read_cb);
  ASSERT_OK(r);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, process_exited);

  if (attempt_failed)
    return -1;

  if (bytes_received != TOTAL_BYTES) {
    fprintf(stderr,
            "pipe data loss: expected %zu bytes, received %zu (missing %zu)\n",
            TOTAL_BYTES,
            bytes_received,
            TOTAL_BYTES - bytes_received);
    return -1;
  }

  return 0;
}


TEST_IMPL(pipe_read_cancel_race) {
  uv_loop_t* loop;
  int attempt;
  int r;

  read_buf = malloc(CHUNK_SIZE);
  ASSERT_NOT_NULL(read_buf);

  loop = uv_default_loop();

  for (attempt = 0; attempt < NUM_ATTEMPTS; attempt++) {
    r = run_one_attempt(loop);
    if (r != 0) {
      fprintf(stderr, "attempt %d of %d FAILED\n", attempt + 1, NUM_ATTEMPTS);
      free(read_buf);
      ASSERT_OK(r);
    }
  }

  free(read_buf);
  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/* Child process helper. Writes NUM_CHUNKS chunks of CHUNK_SIZE bytes to
 * stdout using synchronous blocking writes, then exits. The synchronous
 * writes are essential: they ensure each write lands as a single contiguous
 * chunk from the pipe's perspective, so the parent's ReadFile consistently
 * returns exactly CHUNK_SIZE bytes (triggering the more=1 code path). */
void pipe_read_cancel_race_helper(void) {
  uv_loop_t* loop;
  uv_pipe_t stdout_pipe;
  uv_write_t req;
  uv_buf_t buf;
  char* chunk;
  int i;
  int r;

  chunk = malloc(CHUNK_SIZE);
  ASSERT_NOT_NULL(chunk);
  memset(chunk, 'x', CHUNK_SIZE);

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &stdout_pipe, 0);
  ASSERT_OK(r);
  r = uv_pipe_open(&stdout_pipe, 1);
  ASSERT_OK(r);

  /* Put the pipe in blocking-write mode so that each uv_write() call
   * completes fully before returning control. This is the same approach
   * Node.js / Bun use for stdout when it is a Windows pipe, and it
   * guarantees the chunk boundary is preserved on the wire. */
  r = uv_stream_set_blocking((uv_stream_t*) &stdout_pipe, 1);
  ASSERT_OK(r);

  buf = uv_buf_init(chunk, CHUNK_SIZE);

  for (i = 0; i < NUM_CHUNKS; i++) {
    r = uv_write(&req, (uv_stream_t*) &stdout_pipe, &buf, 1, NULL);
    ASSERT_OK(r);
    /* With blocking mode on Windows, uv_write() performs a synchronous
     * WriteFile and does not queue a request to be processed on the loop.
     * Run the loop once to be safe on other platforms. */
    uv_run(loop, UV_RUN_NOWAIT);
  }

  uv_close((uv_handle_t*) &stdout_pipe, NULL);
  uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  free(chunk);
}
