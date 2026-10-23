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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Regression test for the reverse-cancellation "kicker" that guards data
 * reads on Windows pipes in non-overlapped mode.
 * See comment before uv__pipe_read_data_sync().
 */

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32

TEST_IMPL(pipe_read_cancelled_write_sync) {
  RETURN_SKIP("Windows-specific pipe write-retraction test.");
}

#else /* _WIN32 */

#include <io.h>

#define BIG_WRITE_SIZE (64 * 1024 * 1024)
#define READ_PARK_THRESHOLD (8 * 1024 * 1024)
#define RECOVERY_PAYLOAD "PIPE-RECOVERED!!"
#define RECOVERY_SIZE (sizeof(RECOVERY_PAYLOAD) - 1)
#define POST_CANCEL_TICKS 5

static uv_pipe_t reader;
static uv_timer_t liveness_timer;
static uv_thread_t writer_thread;
static uv_thread_t canceller_thread;

static HANDLE write_end;
static int write_fd = -1;
static HANDLE pended_event;    /* writer -> main: big write is in flight */
static HANDLE reach_event;     /* alloc_cb -> canceller: reader is parked */
static HANDLE cancelled_event; /* writer -> alloc_cb: write cancelled */
static HANDLE resume_event;    /* timer_cb -> writer: loop proven live */

static char read_slab[1024 * 1024];
static char tail[RECOVERY_SIZE];

static uint64_t bytes_received;
static int write_was_pending;
static int reach_signaled;
static int park_over;
static int post_cancel_ticks;
static int loop_proven_live;
static int eof_seen;
static int close_cb_called;
static int sync_writer_done;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void timer_cb(uv_timer_t* timer) {
  if (park_over && !loop_proven_live &&
      ++post_cancel_ticks >= POST_CANCEL_TICKS) {
    /* The loop kept turning after the retraction: the reader did not wedge.
     * Let the writer finish (recovery write + EOF). */
    loop_proven_live = 1;
    ASSERT_NE(0, SetEvent(resume_event));
  }
}


static void sync_writer_thread_proc(void* arg) {
  char* big;
  DWORD transferred;
  BOOL r;

  big = (char*) malloc(BIG_WRITE_SIZE);
  ASSERT_NOT_NULL(big);
  memset(big, 0xAB, BIG_WRITE_SIZE);

  /* Signal before the write: a blocking writer cannot signal afterwards.
   * The reader only starts once this event is set, and only cancels once it
   * has received a threshold of bytes, which guarantees the thread is inside
   * WriteFile by the time CancelSynchronousIo is attempted. */
  ASSERT_NE(0, SetEvent(pended_event));
  r = WriteFile(write_end, big, BIG_WRITE_SIZE, &transferred, NULL);
  if (r == 0)
    ASSERT_EQ((DWORD) ERROR_OPERATION_ABORTED, GetLastError());
  write_was_pending = 1;
  ASSERT_NE(0, SetEvent(cancelled_event));

  /* Wait for the reader's loop to prove it is still alive, then exercise
   * the pipe again. */
  ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(resume_event, 30000));
  ASSERT_NE(0, WriteFile(write_end,
                         RECOVERY_PAYLOAD,
                         (DWORD) RECOVERY_SIZE,
                         &transferred,
                         NULL));
  ASSERT_EQ((DWORD) RECOVERY_SIZE, transferred);

  /* EOF for the reader; the CRT descriptor owns the handle. */
  ASSERT_OK(_close(write_fd));
  write_fd = -1;
  write_end = INVALID_HANDLE_VALUE;
  free(big);
  sync_writer_done = 1;
}


static void sync_canceller_thread_proc(void* arg) {
  uint64_t start;

  /* Wait until the reader is parked inside alloc_cb - between the peek
   * that counted the writer's pended bytes and the ReadFile it sized -
   * then cancel the writer's blocking WriteFile, retracting them.
   * CancelSynchronousIo can miss (ERROR_NOT_FOUND) while the target is
   * between system calls, so retry until the writer acknowledges. */
  ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(reach_event, 30000));
  start = uv_hrtime();
  for (;;) {
    CancelSynchronousIo((HANDLE) writer_thread);
    if (WaitForSingleObject(cancelled_event, 1) == WAIT_OBJECT_0)
      break;
    ASSERT_UINT64_LT(uv_hrtime() - start, (uint64_t) 30 * 1000000000);
  }
}


static void sync_alloc_cb(uv_handle_t* handle,
                          size_t suggested_size,
                          uv_buf_t* buf) {
  if (reach_signaled && !park_over) {
    /* This allocation belongs to a pull cycle whose PeekNamedPipe already
     * counted the writer's pended bytes: libuv calls alloc_cb between
     * that peek and the ReadFile sized from it. Park here while the
     * canceller thread retracts the advertised bytes, so the ReadFile
     * issued when this returns races a drained pipe and, if it blocks,
     * exercises the kicker. */
    ASSERT_NE(0, SetEvent(reach_event));
    ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(cancelled_event, 30000));
    park_over = 1;
  }
  buf->base = read_slab;
  buf->len = sizeof(read_slab);
}


static void sync_read_cb(uv_stream_t* stream,
                         ssize_t nread,
                         const uv_buf_t* buf) {
  size_t keep;
  size_t i;

  if (nread == UV_EOF) {
    eof_seen = 1;
    uv_close((uv_handle_t*) &reader, close_cb);
    uv_close((uv_handle_t*) &liveness_timer, close_cb);
    return;
  }

  ASSERT_GE(nread, 0);

  /* Track the last RECOVERY_SIZE bytes of the stream. */
  if ((size_t) nread >= RECOVERY_SIZE) {
    memcpy(tail, buf->base + nread - RECOVERY_SIZE, RECOVERY_SIZE);
  } else if (nread > 0) {
    keep = RECOVERY_SIZE - (size_t) nread;
    memmove(tail, tail + RECOVERY_SIZE - keep, keep);
    for (i = 0; i < (size_t) nread; i++)
      tail[keep + i] = buf->base[i];
  }

  bytes_received += (uint64_t) nread;

  /* Once a threshold has been drained, direct the NEXT pull cycle's
   * alloc_cb to park for the retraction: the writer is guaranteed to
   * still be blocked inside its WriteFile with most of its bytes
   * advertised-but-unread. */
  if (bytes_received >= READ_PARK_THRESHOLD)
    reach_signaled = 1;
}


TEST_IMPL(pipe_read_cancelled_write_sync) {
  uv_file fds[2];

  /* Both ends non-overlapped. */
  ASSERT_OK(uv_pipe(fds, 0, 0));

  write_fd = fds[1];
  write_end = (HANDLE) _get_osfhandle(fds[1]);
  ASSERT_PTR_NE(write_end, INVALID_HANDLE_VALUE);

  pended_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  reach_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  cancelled_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  resume_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  ASSERT_NOT_NULL(pended_event);
  ASSERT_NOT_NULL(reach_event);
  ASSERT_NOT_NULL(cancelled_event);
  ASSERT_NOT_NULL(resume_event);

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &reader, 0));
  ASSERT_OK(uv_pipe_open(&reader, fds[0]));

  ASSERT_OK(uv_timer_init(uv_default_loop(), &liveness_timer));
  ASSERT_OK(uv_timer_start(&liveness_timer, timer_cb, 10, 10));

  ASSERT_OK(uv_thread_create(&writer_thread, sync_writer_thread_proc, NULL));
  ASSERT_OK(uv_thread_create(&canceller_thread,
                             sync_canceller_thread_proc,
                             NULL));
  ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(pended_event, 30000));
  ASSERT_OK(uv_read_start((uv_stream_t*) &reader, sync_alloc_cb, sync_read_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(uv_thread_join(&writer_thread));
  ASSERT_OK(uv_thread_join(&canceller_thread));

  ASSERT_EQ(1, write_was_pending);
  ASSERT_EQ(1, reach_signaled);
  ASSERT_EQ(1, park_over);
  ASSERT_EQ(1, loop_proven_live);
  ASSERT_EQ(1, eof_seen);
  ASSERT_EQ(1, sync_writer_done);
  ASSERT_EQ(2, close_cb_called);

  ASSERT_UINT64_GE(bytes_received, READ_PARK_THRESHOLD + RECOVERY_SIZE);
  ASSERT_UINT64_LT(bytes_received - RECOVERY_SIZE, (uint64_t) BIG_WRITE_SIZE);
  ASSERT_OK(memcmp(tail, RECOVERY_PAYLOAD, RECOVERY_SIZE));

  CloseHandle(pended_event);
  CloseHandle(reach_event);
  CloseHandle(cancelled_event);
  CloseHandle(resume_event);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif /* _WIN32 */
