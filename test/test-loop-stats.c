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

#include "limits.h"
#include "stdio.h"

static uv_timer_t timer_handle;
static uv_check_t check_handle;

static void timer_cb(uv_timer_t* handle) {}
static void check_cb(uv_check_t* handle) {}

static unsigned int on_stats_cb_count_called = 0;
static size_t timers = 0;

static void stats_cb(uv_loop_stats_data_t* stats, void* data) {
  ASSERT(stats->tick_start > 0);
  ASSERT(stats->tick_end >= stats->tick_start);
  ASSERT(stats->pending_start > 0);
  ASSERT(stats->pending_end >= stats->pending_start);
  ASSERT(stats->idle_start > 0);
  ASSERT(stats->idle_end >= stats->idle_start);
  ASSERT(stats->prepare_start > 0);
  ASSERT(stats->prepare_end >= stats->prepare_start);
  ASSERT(stats->poll_start > 0);
  ASSERT(stats->poll_end >= stats->poll_start);
  ASSERT(stats->check_start > 0);
  ASSERT(stats->check_end >= stats->check_start);
  ASSERT(stats->timers1_start > 0);
  ASSERT(stats->timers1_end >= stats->timers1_start);
  ASSERT(stats->timers2_start == 0);
  ASSERT(stats->timers2_end == 0);

  ASSERT(stats->pending_count == 0);
  ASSERT(stats->idle_count == 0);
  ASSERT(stats->prepare_count == 0);
  ASSERT(stats->check_count == 1);

  timers += stats->timers_count;
  on_stats_cb_count_called++;
}

TEST_IMPL(loop_stats) {
  uv_loop_stats_t config;
  config.cb = stats_cb;

  uv_loop_configure(uv_default_loop(), UV_LOOP_STATS, &config);

  uv_check_init(uv_default_loop(), &check_handle);
  uv_check_start(&check_handle, check_cb);
  uv_unref((uv_handle_t*)(&check_handle));

  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(on_stats_cb_count_called);
  ASSERT(timers == 1);

  return 0;
}
