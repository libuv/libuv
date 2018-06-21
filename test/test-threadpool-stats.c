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
 
static int submitted;
static int started;
static int done;
#define START_SIZE 8
static int idle_max;
static int idle_min = 2 * START_SIZE;
static int queued_max;
 
static void update(int queued, int idle) {
  if (queued > queued_max) queued_max = queued;
  if (idle > idle_max) idle_max = idle;
  if (idle < idle_min) idle_min = idle;
}
 
static void stats_submit_cb(unsigned queued, unsigned idle, void* data) {
  submitted++;
  update(queued, idle);
}
 
static void stats_start_cb(unsigned queued, unsigned idle, void* data) {
  started++;
  update(queued, idle);
}
 
static void stats_done_cb(unsigned queued, unsigned idle, void* data) {
  done++;
  update(queued, idle);
}
 
 
TEST_IMPL(threadpool_stats) {
  uv_threadpool_stats_t stats;
  stats.done_cb = stats_done_cb;
  stats.start_cb = stats_start_cb;
  stats.submit_cb = stats_submit_cb;
  stats.data = &stats;

  uv_loop_configure(uv_default_loop(), UV_THREADPOOL_STATS, &stats);
 
  saturate_threadpool(START_SIZE);
  uv_sleep(500); /* Give idle threads time to grab work items and block. */
  unblock_threadpool();
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  uv_loop_configure(uv_default_loop(), UV_THREADPOOL_STATS, NULL); 
  
  ASSERT(submitted == START_SIZE);
  ASSERT(started == START_SIZE);
  ASSERT(done == START_SIZE);
  ASSERT(queued_max >= (int) START_SIZE / 2);
  ASSERT(idle_max == START_SIZE / 2);
  ASSERT(idle_min == 0);
 
  MAKE_VALGRIND_HAPPY();
  return 0;
}