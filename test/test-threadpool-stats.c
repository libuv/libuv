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

static uv_work_t pause_reqs[8];
static uv_sem_t pause_sems[ARRAY_SIZE(pause_reqs)];

static void work_cb(uv_work_t* req) {
  uv_sem_wait(pause_sems + (req - pause_reqs));
}

static void done_cb(uv_work_t* req, int status) {
  uv_sem_destroy(pause_sems + (req - pause_reqs));
}

static void saturate_threadpool(void) {
  uv_loop_t* loop;
  size_t i;

  loop = uv_default_loop();
  for (i = 0; i < ARRAY_SIZE(pause_reqs); i += 1) {
    ASSERT(0 == uv_sem_init(pause_sems + i, 0));
    ASSERT(0 == uv_queue_work(loop, pause_reqs + i, work_cb, done_cb));
  }
}

static void unblock_threadpool(void) {
  size_t i;

  for (i = 0; i < ARRAY_SIZE(pause_reqs); i += 1)
    uv_sem_post(pause_sems + i);
}

int submitted;
int started;
int done;
int idle_max;
int idle_min = 2 * ARRAY_SIZE(pause_reqs);
int queued_max;

static void update(int queued, int idle) {
  if (queued > queued_max) queued_max = queued;
  if (idle > idle_max) idle_max = idle;
  if (idle < idle_min) idle_min = idle;
}

static void stats_submit_cb(int queued, int idle, void* data) {
  fprintf(stderr, "submit: q %d i %d\n", queued, idle);
  submitted++;
  update(queued, idle);
}

static void stats_start_cb(int queued, int idle, void* data) {
  fprintf(stderr, "start: q %d i %d\n", queued, idle);
  started++;
  update(queued, idle);
}

static void stats_done_cb(int queued, int idle, void* data) {
  fprintf(stderr, "done: q %d i %d\n", queued, idle);
  done++;
  update(queued, idle);
}


TEST_IMPL(threadpool_stats) {
  /* uv_loop_t* loop; */
  /* uv_work_t req; */
  char buf[64];
  uv_queue_stats_t stats;
  stats.submit_cb = stats_submit_cb;
  stats.start_cb = stats_start_cb;
  stats.done_cb = stats_done_cb;

  /* Set here, because stats_start initializes the threadpool */
  snprintf(buf,
           sizeof(buf),
           "UV_THREADPOOL_SIZE=%lu",
           (unsigned long)ARRAY_SIZE(pause_reqs) / 2);
  putenv(buf);

  uv_queue_stats_start(&stats);

  saturate_threadpool();
  uv_sleep(500); /* Give idle threads time to grab work items and block. */
  unblock_threadpool();
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  uv_queue_stats_stop(&stats);

  printf(
      "submitted %d started %d done %d queued_max %d idle_max %d idle_min %d\n",
      submitted, started, done, queued_max, idle_max, idle_min);

  ASSERT(submitted == ARRAY_SIZE(pause_reqs));
  ASSERT(started == ARRAY_SIZE(pause_reqs));
  ASSERT(done == ARRAY_SIZE(pause_reqs));
  ASSERT(queued_max >= (int) ARRAY_SIZE(pause_reqs) / 2);
  ASSERT(idle_max == ARRAY_SIZE(pause_reqs) / 2);
  ASSERT(idle_min == 0);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
