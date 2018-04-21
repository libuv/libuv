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

static int submitted = 0;
static int started = 0;
static int done = 0;
#define START_SIZE 8
static unsigned idle_max = 0;
static unsigned idle_min = 2 * START_SIZE;
static unsigned queued_max = 0;
 
static void update(const uv_trace_info_t* info) {
  const uv_trace_threadpool_info_t* trace_info =
     (const uv_trace_threadpool_info_t*)info;
  if (trace_info->queued > queued_max) queued_max = trace_info->queued;
  if (trace_info->idle_threads > idle_max) idle_max = trace_info->idle_threads;
  if (trace_info->idle_threads < idle_min) idle_min = trace_info->idle_threads;
}
 
static void trace_cb(const uv_trace_info_t* info, void* data) {
  switch (info->type) {
    case UV_TRACE_THREADPOOL_SUBMIT:
      submitted++;
      update(info);
      break;
    case UV_TRACE_THREADPOOL_START:
      started++;
      update(info);
      break;
    case UV_TRACE_THREADPOOL_DONE:
      done++;
      update(info);
      break;
    default:
      break;
  }
}
 
TEST_IMPL(threadpool_trace) {
  uv_threadpool_trace_t trace;
  trace.cb = trace_cb;
  uv_loop_configure(uv_default_loop(), UV_THREADPOOL_TRACE, &trace);
 
  saturate_threadpool(START_SIZE);
  uv_sleep(500); /* Give idle threads time to grab work items and block. */
  unblock_threadpool();
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));
 
  uv_loop_configure(uv_default_loop(), UV_THREADPOOL_TRACE, NULL); 
  
  ASSERT(submitted == START_SIZE);
  ASSERT(started == START_SIZE);
  ASSERT(done == START_SIZE);
  ASSERT(queued_max >= (int) START_SIZE / 2);
  ASSERT(idle_max == START_SIZE / 2);
  ASSERT(idle_min == 0);
 
  MAKE_VALGRIND_HAPPY();
  return 0;
} 