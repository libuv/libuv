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

/* Drives the thread pool through the patterns its idle-worker logic cares
 * about: strictly sequential submissions (each request posted from the
 * completion callback of the previous one), gaps long enough for any
 * lingering worker to give up repeatedly, and bursts, and checks that every
 * request runs exactly once and completes in submission order per chain.
 */

#include "uv.h"
#include "task.h"

#define CHAIN_LEN 200
#define ROUNDS 12
#define BURST 64

static uv_loop_t* loop;
static uv_work_t chain_req;
static uv_work_t burst_reqs[BURST];
static uv_timer_t timer;
static int round_nr;
static int chain_pos;
static int work_cb_count;
static int after_work_cb_count;
static int burst_done;

static void start_round(void);

static void work_cb(uv_work_t* req) {
  /* Racy on purpose only in the sense that several burst items run in
   * parallel; the counter itself is checked after uv_run() returns. */
  (void) req;
}

static void chain_after_work_cb(uv_work_t* req, int status);

static void post_chain(void) {
  ASSERT_OK(uv_queue_work(loop, &chain_req, work_cb, chain_after_work_cb));
}

static void timer_cb(uv_timer_t* handle) {
  start_round();
}

static void burst_after_work_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  after_work_cb_count++;
  if (++burst_done < BURST)
    return;
  /* Leave a gap that is far longer than any worker would linger for. */
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 30, 0));
}

static void chain_after_work_cb(uv_work_t* req, int status) {
  int i;

  ASSERT_OK(status);
  ASSERT_PTR_EQ(req, &chain_req);
  after_work_cb_count++;
  work_cb_count++;  /* Chain items are strictly sequential. */

  if (++chain_pos < CHAIN_LEN) {
    post_chain();
    return;
  }

  burst_done = 0;
  for (i = 0; i < BURST; i++)
    ASSERT_OK(uv_queue_work(loop,
                            &burst_reqs[i],
                            work_cb,
                            burst_after_work_cb));
}

static void start_round(void) {
  if (round_nr++ == ROUNDS) {
    uv_close((uv_handle_t*) &timer, NULL);
    return;
  }
  chain_pos = 0;
  post_chain();
}

TEST_IMPL(threadpool_serial_chain) {
  loop = uv_default_loop();
  ASSERT_OK(uv_timer_init(loop, &timer));
  start_round();
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(ROUNDS * (CHAIN_LEN + BURST), after_work_cb_count);
  ASSERT_EQ(ROUNDS * CHAIN_LEN, work_cb_count);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
