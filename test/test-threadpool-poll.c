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

/* The loop may poll briefly for an imminent thread pool completion before it
 * blocks. That must stay strictly bounded: with a slow request in flight, a
 * repeating timer has to keep firing while we wait for it. If the polling
 * were unbounded (or its misses added up), the loop would sit in it until the
 * request completed and no timer callback would run before the completion
 * callback. Counting ticks that land before the completion checks exactly
 * that without depending on how precise timers are on the CI machine.
 */

#include "uv.h"
#include "task.h"

#define SLOW_WORK_MS 400
#define MIN_TICKS_BEFORE_DONE 20  /* ~400 expected; Windows timers are coarse */

static uv_work_t slow_req;
static uv_timer_t timer;
static int ticks;
static int ticks_before_done;
static int work_done;

static void slow_work_cb(uv_work_t* req) {
  uv_sleep(SLOW_WORK_MS);
}

static void slow_after_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  work_done = 1;
  ticks_before_done = ticks;
  uv_close((uv_handle_t*) &timer, NULL);
}

static void timer_cb(uv_timer_t* handle) {
  ticks++;
}

TEST_IMPL(threadpool_poll_before_block_is_bounded) {
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_OK(uv_queue_work(loop, &slow_req, slow_work_cb, slow_after_cb));
  ASSERT_OK(uv_timer_init(loop, &timer));
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 1, 1));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT(work_done);
  /* A 1 ms repeating timer with a 400 ms request in flight the whole time. */
  ASSERT_GE(ticks_before_done, MIN_TICKS_BEFORE_DONE);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/* Sequential requests whose work takes about as long as the loop's pre-block
 * polling window, so that completions keep landing right around the moment
 * the window closes and the worker has to decide whether a wakeup write is
 * needed. A lost wakeup here would stall the chain (and time the test out).
 */

#define BOUNDARY_REQS 3000

static uv_work_t boundary_req;
static int boundary_count;

static void boundary_work_cb(uv_work_t* req) {
  uint64_t spin_until;
  /* 4..16 us of "work", varying, straddling the 10 us window. */
  spin_until = uv_hrtime() + 4000 + (boundary_count * 7919 % 12000);
  while (uv_hrtime() < spin_until)
    ;
}

static void boundary_after_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  if (++boundary_count == BOUNDARY_REQS)
    return;
  ASSERT_OK(uv_queue_work(req->loop, req, boundary_work_cb, boundary_after_cb));
}

TEST_IMPL(threadpool_poll_window_boundary) {
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_OK(uv_queue_work(loop,
                          &boundary_req,
                          boundary_work_cb,
                          boundary_after_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(BOUNDARY_REQS, boundary_count);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
