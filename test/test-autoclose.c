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

static int timer1_cb_called = 0;
static int timer2_cb_called = 0;
static int timer1_close_cb_called = 0;
static int timer2_close_cb_called = 0;

static int also_close_timer1 = 0;

static uv_timer_t timer1_handle;
static uv_timer_t timer2_handle;


static void timer2_close_cb(uv_handle_t *handle) {
  ASSERT(handle == (uv_handle_t*) &timer2_handle);
  ++timer2_close_cb_called;
}


static void timer1_close_cb(uv_handle_t *handle) {
  ASSERT(handle == (uv_handle_t*) &timer1_handle);
  ++timer1_close_cb_called;
}


static void timer2_cb(uv_timer_t* timer) {
  ASSERT(timer == &timer2_handle);
  ++timer2_cb_called;
  uv_close((uv_handle_t*) timer, &timer2_close_cb);

  if (also_close_timer1) {
    uv_close((uv_handle_t*) &timer1_handle, &timer1_close_cb);
  }
}


static void timer1_cb(uv_timer_t* timer) {
  ASSERT(timer == &timer1_handle);
  ++timer1_cb_called;

  uv_autoclose((uv_handle_t*) timer, &timer1_close_cb);

  ASSERT(uv_timer_init(timer->loop, &timer2_handle) == 0);
  ASSERT(uv_timer_start(&timer2_handle, &timer2_cb, 0, 0) == 0);
}


TEST_IMPL(autoclose) {
  uv_loop_t* loop;
  loop = uv_default_loop();

  ASSERT(uv_timer_init(loop, &timer1_handle) == 0);
  ASSERT(uv_timer_start(&timer1_handle, timer1_cb, 0, 0) == 0);

  ASSERT(timer1_cb_called == 0);
  ASSERT(timer2_cb_called == 0);
  ASSERT(timer1_close_cb_called == 0);
  ASSERT(timer2_close_cb_called == 0);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(timer1_cb_called >= 1);
  ASSERT(timer2_cb_called >= 1);
  ASSERT(timer1_close_cb_called == 1);
  ASSERT(timer2_close_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(autoclose_manual_close) {
  uv_loop_t* loop;
  loop = uv_default_loop();

  ASSERT(uv_timer_init(loop, &timer1_handle) == 0);
  ASSERT(uv_timer_start(&timer1_handle, timer1_cb, 0, 0) == 0);

  ASSERT(timer1_cb_called == 0);
  ASSERT(timer2_cb_called == 0);
  ASSERT(timer1_close_cb_called == 0);
  ASSERT(timer2_close_cb_called == 0);

  also_close_timer1 = 1;

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(timer1_cb_called >= 1);
  ASSERT(timer2_cb_called >= 1);
  ASSERT(timer1_close_cb_called == 1);
  ASSERT(timer2_close_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
