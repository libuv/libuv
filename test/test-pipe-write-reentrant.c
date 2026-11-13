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

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
static uv_pipe_t reader;
static uv_pipe_t writer;
static uv_write_t writes[3];
static uv_write_t cancelled_writes[2];
static const char expected[] = "firstsecondthird";
static char* messages[] = { "first", "second", "third" };
static size_t received;
static unsigned int completed;
static unsigned int cancelled;
static unsigned int closed;
static int cancel_queued;

static void close_cb(uv_handle_t* handle) {
  ASSERT(handle == (uv_handle_t*) &reader ||
         handle == (uv_handle_t*) &writer);
  closed++;
}

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  ASSERT_PTR_EQ(handle, (uv_handle_t*) &reader);
  buf->base = malloc(suggested_size);
  ASSERT_NOT_NULL(buf->base);
  buf->len = (unsigned int) suggested_size;
}

static void read_cb(uv_stream_t* stream,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  ASSERT_PTR_EQ(stream, (uv_stream_t*) &reader);
  if (nread > 0) {
    ASSERT_LE(received + (size_t) nread, sizeof(expected) - 1);
    ASSERT_MEM_EQ(buf->base, expected + received, nread);
    received += (size_t) nread;
  } else if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);
    ASSERT_EQ(received, sizeof(expected) - 1);
    uv_close((uv_handle_t*) stream, close_cb);
  }
  free(buf->base);
}

static void cancelled_write_cb(uv_write_t* req, int status) {
  ASSERT(req == &cancelled_writes[0] || req == &cancelled_writes[1]);
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &writer);
  ASSERT_EQ(status, UV_ECANCELED);
  ASSERT_EQ(uv_write_nwritten(req), 0);
  cancelled++;
}

static void write_cb(uv_write_t* req, int status) {
  uv_buf_t buf;
  uv_buf_t cancelled_buf;

  ASSERT_OK(status);
  ASSERT_PTR_EQ(req, &writes[completed]);
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &writer);
  ASSERT_EQ(uv_write_nwritten(req), strlen(messages[completed]));
  completed++;
  if (completed == ARRAY_SIZE(writes)) {
    ASSERT_EQ(uv_stream_get_write_queue_size((uv_stream_t*) &writer), 0);
    uv_close((uv_handle_t*) &writer, close_cb);
    return;
  }

  /* The completing write is still counted as pending during this callback,
   * but there is no longer an active write on the thread pool. */
  ASSERT_GE(writer.stream.conn.write_reqs_pending, 1);
  ASSERT_NULL(writer.pipe.conn.non_overlapped_write_active);
  buf = uv_buf_init(messages[completed],
                    (unsigned int) strlen(messages[completed]));
  ASSERT_OK(uv_write(&writes[completed],
                     (uv_stream_t*) &writer,
                     &buf,
                     1,
                     write_cb));

  if (cancel_queued) {
    /* This write is queued behind the one just dispatched. Cancellation
     * must not prevent the next callback from starting another write. */
    cancelled_buf = uv_buf_init("cancelled", 9);
    ASSERT_OK(uv_write(&cancelled_writes[completed - 1],
                       (uv_stream_t*) &writer,
                       &cancelled_buf,
                       1,
                       cancelled_write_cb));
    ASSERT_OK(uv_cancel((uv_req_t*) &cancelled_writes[completed - 1]));
  }
}

static int run_test(int cancel) {
  uv_file fds[2];
  uv_buf_t buf;
  uv_loop_t* loop;

  loop = uv_default_loop();
  cancel_queued = cancel;
  ASSERT_OK(uv_pipe(fds, 0, 0));
  ASSERT_OK(uv_pipe_init(loop, &reader, 0));
  ASSERT_OK(uv_pipe_init(loop, &writer, 0));
  ASSERT_OK(uv_pipe_open(&reader, fds[0]));
  ASSERT_OK(uv_pipe_open(&writer, fds[1]));
  ASSERT_OK(uv_read_start((uv_stream_t*) &reader, alloc_cb, read_cb));
  buf = uv_buf_init(messages[0], (unsigned int) strlen(messages[0]));
  ASSERT_OK(uv_write(&writes[0], (uv_stream_t*) &writer, &buf, 1, write_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(completed, ARRAY_SIZE(writes));
  ASSERT_EQ(cancelled, cancel ? ARRAY_SIZE(cancelled_writes) : 0);
  ASSERT_EQ(closed, 2);
  ASSERT_EQ(received, sizeof(expected) - 1);
  ASSERT_EQ(uv_stream_get_write_queue_size((uv_stream_t*) &writer), 0);
  ASSERT_EQ(writer.stream.conn.write_reqs_pending, 0);
  ASSERT_NULL(writer.pipe.conn.non_overlapped_write_active);
  ASSERT_NULL(writer.pipe.conn.non_overlapped_writes_tail);
  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
#endif

TEST_IMPL(pipe_write_reentrant) {
#ifdef _WIN32
  return run_test(0);
#else
  RETURN_SKIP("Windows non-overlapped pipe test");
#endif
}

TEST_IMPL(pipe_write_reentrant_cancel) {
#ifdef _WIN32
  return run_test(1);
#else
  RETURN_SKIP("Windows non-overlapped pipe test");
#endif
}
