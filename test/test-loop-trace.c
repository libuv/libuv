/* Copyright The libuv project and contributors. All rights reserved.
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
#include <string.h>

static uv_loop_t loop;

static void timer_cb(uv_timer_t* handle) {}
static void check_cb(uv_check_t* handle) {}
static void prepare_cb(uv_prepare_t* handle) {}

static int saw_start_event;
static int saw_end_event;
static size_t tick_count;
static size_t timers_count;
static size_t check_count;
static size_t idle_count;
static size_t prepare_count;
static size_t pending_count;

static void trace_start_cb(const uv_trace_info_t* info, void* data) {
  ASSERT(&loop == data);
  saw_start_event |= 1 << info->type;
  switch (info->type) {
    case UV_TRACE_TICK:
      tick_count++;
      break;
    case UV_TRACE_TIMERS:
      ASSERT(0 == ((const uv_trace_timers_info_t*)info)->count);
      break;
    case UV_TRACE_CHECK:
      ASSERT(0 == ((const uv_trace_check_info_t*)info)->count);
      break;
    case UV_TRACE_IDLE:
      ASSERT(0 == ((const uv_trace_idle_info_t*)info)->count);
      break;
    case UV_TRACE_PREPARE:
      ASSERT(0 == ((const uv_trace_prepare_info_t*)info)->count);
      break;
    case UV_TRACE_PENDING:
      ASSERT(0 == ((const uv_trace_pending_info_t*)info)->count);
      break;
    case UV_TRACE_POLL:
      ASSERT(0 <= ((const uv_trace_poll_info_t*)info)->timeout);
      break;
    default:
      /* Fallthrough */
      break;
  }
}

static void trace_end_cb(const uv_trace_info_t* info, void* data) {
  ASSERT(&loop == data);
  saw_end_event |= 1 << info->type;
  switch (info->type) {
    case UV_TRACE_TICK:
      break;
    case UV_TRACE_TIMERS:
      timers_count += ((const uv_trace_timers_info_t*)info)->count;
      break;
    case UV_TRACE_CHECK:
      check_count += ((const uv_trace_check_info_t*)info)->count;
      break;
    case UV_TRACE_IDLE:
      idle_count += ((const uv_trace_idle_info_t*)info)->count;
      break;
    case UV_TRACE_PREPARE:
      prepare_count += ((const uv_trace_prepare_info_t*)info)->count;
      break;
    case UV_TRACE_PENDING:
      pending_count += ((const uv_trace_pending_info_t*)info)->count;
      break;
    case UV_TRACE_POLL:
      ASSERT(0 <= ((const uv_trace_poll_info_t*)info)->timeout);
      break;
    default:
      /* Fallthrough */
      break;
  }
}

TEST_IMPL(loop_trace) {
  uv_timer_t timer_handle;
  uv_check_t check_handle;
  uv_prepare_t prepare_handle;

  uv_loop_trace_t config;
  uv_loop_init(&loop);

  config.data = &loop;
  config.start_cb = trace_start_cb;
  config.end_cb = trace_end_cb;
  uv_loop_configure(&loop, UV_LOOP_TRACE, &config);

  uv_check_init(&loop, &check_handle);
  uv_check_start(&check_handle, check_cb);
  uv_unref((uv_handle_t*)(&check_handle));

  uv_prepare_init(&loop, &prepare_handle);
  uv_prepare_start(&prepare_handle, prepare_cb);
  uv_unref((uv_handle_t*)(&prepare_handle));

  uv_timer_init(&loop, &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 10, 0);

  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);

  ASSERT(saw_start_event & (1 << UV_TRACE_TICK));
  ASSERT(saw_start_event & (1 << UV_TRACE_TIMERS));
  ASSERT(saw_start_event & (1 << UV_TRACE_CHECK));
  ASSERT(saw_start_event & (1 << UV_TRACE_IDLE));
  ASSERT(saw_start_event & (1 << UV_TRACE_PREPARE));
  ASSERT(!(saw_start_event & (1 << UV_TRACE_PENDING)));
  ASSERT(saw_start_event & (1 << UV_TRACE_POLL));

  ASSERT(saw_end_event & (1 << UV_TRACE_TICK));
  ASSERT(saw_end_event & (1 << UV_TRACE_TIMERS));
  ASSERT(saw_end_event & (1 << UV_TRACE_CHECK));
  ASSERT(saw_end_event & (1 << UV_TRACE_IDLE));
  ASSERT(saw_end_event & (1 << UV_TRACE_PREPARE));
  ASSERT(!(saw_end_event & (1 << UV_TRACE_PENDING)));
  ASSERT(saw_end_event & (1 << UV_TRACE_POLL));

  ASSERT(2 == tick_count);
  ASSERT(1 == timers_count);
  ASSERT(2 == check_count);
  ASSERT(0 == idle_count);
  ASSERT(2 == prepare_count);
  ASSERT(0 == pending_count);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
