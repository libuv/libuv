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

/* Rounds of bursts with idle gaps in between, plus a strictly sequential
 * chain and a few cancellations, so the thread pool's wake-up bookkeeping goes
 * through all of: many posts while workers are still waking, all workers idle,
 * queue found empty, and uv_cancel() removing queued items. Every request must
 * complete exactly once (or be reported cancelled).
 */

#include "uv.h"
#include "task.h"

#define ROUNDS 10
#define BURST 128
#define CHAIN 100

static uv_loop_t* loop;
static uv_timer_t timer;
static uv_work_t burst_reqs[BURST];
static uv_work_t chain_req;
static int round_nr;
static int burst_done;
static int chain_pos;
static int done_count;
static int cancelled_count;

static void start_round(void);

static void work_cb(uv_work_t* req) {
  (void) req;
}

static void slow_work_cb(uv_work_t* req) {
  uv_sleep(1);
}

static void timer_cb(uv_timer_t* handle) {
  start_round();
}

static void chain_after_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  done_count++;
  if (++chain_pos < CHAIN) {
    ASSERT_OK(uv_queue_work(loop, &chain_req, work_cb, chain_after_cb));
    return;
  }
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 20, 0));
}

static void burst_after_cb(uv_work_t* req, int status) {
  if (status == UV_ECANCELED)
    cancelled_count++;
  else
    ASSERT_OK(status);
  done_count++;
  if (++burst_done < BURST)
    return;
  chain_pos = 0;
  ASSERT_OK(uv_queue_work(loop, &chain_req, work_cb, chain_after_cb));
}

static void start_round(void) {
  int i;

  if (round_nr++ == ROUNDS) {
    uv_close((uv_handle_t*) &timer, NULL);
    return;
  }
  burst_done = 0;
  for (i = 0; i < BURST; i++)
    ASSERT_OK(uv_queue_work(loop,
                            &burst_reqs[i],
                            i % 16 == 0 ? slow_work_cb : work_cb,
                            burst_after_cb));
  /* Some of these are still queued; cancelling exercises the path where the
   * pool's queued-item count can only be corrected lazily. */
  for (i = BURST - 8; i < BURST; i++)
    uv_cancel((uv_req_t*) &burst_reqs[i]);
}

TEST_IMPL(threadpool_burst_rounds) {
  loop = uv_default_loop();
  ASSERT_OK(uv_timer_init(loop, &timer));
  start_round();
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(ROUNDS * (BURST + CHAIN), done_count);
  ASSERT_LE(cancelled_count, ROUNDS * 8);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
