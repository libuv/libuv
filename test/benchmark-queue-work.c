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

#include "task.h"
#include "uv.h"

static int done = 0;
static unsigned events = 0;
static unsigned result;

static unsigned fastrand(void) {
  static unsigned g = 0;
  g = g * 214013 + 2531011;
  return g;
}

#define SIZE (1024*1024)
uv_work_t work[SIZE];

static void work_single(uv_work_t* req) {
  req->data = &result;
  *(unsigned*)req->data = fastrand();
}

static void reschedule_work(uv_work_t* req, int status) {
  events++;
  if (!done)
    ASSERT_EQ(0, uv_queue_work(req->loop, req, work_single, reschedule_work));
}

static void work_chained(uv_work_t* req) {
  work_single(req);
  events++;
  if (!done) {
    ASSERT_EQ(0, uv_queue_work(req->loop, &work[events % SIZE], work_chained, NULL));
  }
}

static void timer_cb(uv_timer_t* handle) { done = 1; }

BENCHMARK_IMPL(queue_work) {
  uv_timer_t timer_handle;
  uv_loop_t* loop;
  int timeout;

  loop = uv_default_loop();
  timeout = 5000;

  ASSERT_EQ(0, uv_timer_init(loop, &timer_handle));
  ASSERT_EQ(0, uv_timer_start(&timer_handle, timer_cb, timeout, 0));

  /* 
   * schedule work unit and then reschedule next work unit
   * in the main event loop callback
   */
  ASSERT_EQ(0, uv_queue_work(loop, &work[0], work_single, reschedule_work));
  ASSERT_EQ(0, uv_run(loop, UV_RUN_DEFAULT));

  printf("%s ping-pong async jobs in %.1f seconds (%s/s)\n", fmt(events),
         timeout / 1000., fmt(events / (timeout / 1000.)));
  events = 0;
  done = 0;

  ASSERT_EQ(0, uv_timer_init(loop, &timer_handle));
  ASSERT_EQ(0, uv_timer_start(&timer_handle, timer_cb, timeout, 0));

  /*
   * schedule work unit and then immediately reschedule next work unit
   * without main event loop callback
   */
  ASSERT_EQ(0, uv_queue_work(loop, &work[0], work_chained, NULL));
  ASSERT_EQ(0, uv_run(loop, UV_RUN_DEFAULT));

  printf("%s chained async jobs in %.1f seconds (%s/s)\n", fmt(events), timeout / 1000.,
         fmt(events / (timeout / 1000.)));

  MAKE_VALGRIND_HAPPY();
  return 0;
}
