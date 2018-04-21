/* Copyright (c) 2014, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "uv.h"
#include "task.h"
#include <string.h>

static uv_loop_t loop;

static void timer_cb(uv_timer_t* handle) {}
static void check_cb(uv_check_t* handle) {}
static void prepare_cb(uv_prepare_t* handle) {}

static int saw_start_event = 0;
static int saw_end_event = 0;
static size_t tick_count = 0;
static size_t timers_count = 0;
static size_t check_count = 0;
static size_t idle_count = 0;
static size_t prepare_count = 0;
static size_t pending_count = 0;

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
