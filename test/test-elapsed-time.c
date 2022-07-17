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

int count = 0;

void loop_elapsed_time_callback(uv_loop_t *loop, uint64_t time, enum uv_loop_phase pahse) {
  count++;
}

static void noop(uv_timer_t* handle) {}

TEST_IMPL(loop_elapsed_time) {
  uv_loop_t* loop = uv_default_loop();
  uv_timer_t timer;
  uv_set_loop_elapsed_time_callback(loop, loop_elapsed_time_callback);
  ASSERT_EQ(0, uv_timer_init(loop, &timer));
  ASSERT_EQ(0, uv_timer_start(&timer, noop, 0, 0));
  ASSERT_EQ(0, uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(7, count);
  MAKE_VALGRIND_HAPPY();
  return 0;
}
