/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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


static int close_cb_called = 0;
static int repeat_1_cb_called = 0;
static int repeat_2_cb_called = 0;

static int repeat_2_cb_allowed = 0;

static uv_timer_t dummy, repeat_1, repeat_2;

static uint64_t start_time;


static void close_cb(uv_handle_t* handle) {
  ASSERT(handle != NULL);

  close_cb_called++;
}


static void repeat_1_cb(uv_timer_t* handle) {
  int r;

  ASSERT(handle == &repeat_1);
  ASSERT(uv_timer_get_repeat((uv_timer_t*)handle) == 50);

  fprintf(stderr, "repeat_1_cb called after %ld ms\n",
          (long int)(uv_now(uv_default_loop()) - start_time));
  fflush(stderr);

  repeat_1_cb_called++;

  r = uv_timer_again(&repeat_2);//重启repeat 2
  ASSERT(r == 0);

  //一直重启repeat 2所以repeat 2不能获得回调
  //repeat 1获得10次回调后 关闭了，所以repeat 2 可以获得回调了
  if (repeat_1_cb_called == 10) {
    uv_close((uv_handle_t*)handle, close_cb);
    /* 不会再重启repeat 2了所以 repeat 2可以获得回调了 */
    repeat_2_cb_allowed = 1;
    return;
  }
}


static void repeat_2_cb(uv_timer_t* handle) {
  ASSERT(handle == &repeat_2);
  ASSERT(repeat_2_cb_allowed);

  fprintf(stderr, "repeat_2_cb called after %ld ms\n",
          (long int)(uv_now(uv_default_loop()) - start_time));
  fflush(stderr);

  repeat_2_cb_called++;

  if (uv_timer_get_repeat(&repeat_2) == 0) {
    ASSERT(0 == uv_is_active((uv_handle_t*) handle));
    uv_close((uv_handle_t*)handle, close_cb);
    return;
  }

  fprintf(stderr, "uv_timer_get_repeat %ld ms\n",
          (long int)uv_timer_get_repeat(&repeat_2));
  fflush(stderr);
  ASSERT(uv_timer_get_repeat(&repeat_2) == 100);

  /* 下次循环才会生效 */
  uv_timer_set_repeat(&repeat_2, 0);
}


TEST_IMPL(timer_again) {
  int r;

  start_time = uv_now(uv_default_loop());
  ASSERT(0 < start_time);

  /* 验证是否可重启一个没有启动的loop：结果是不能重启 */
  r = uv_timer_init(uv_default_loop(), &dummy);
  ASSERT(r == 0);
  r = uv_timer_again(&dummy);
  ASSERT(r == UV_EINVAL);
  uv_unref((uv_handle_t*)&dummy);

  /* 启动repeat 1 */
  r = uv_timer_init(uv_default_loop(), &repeat_1);
  ASSERT(r == 0);
  r = uv_timer_start(&repeat_1, repeat_1_cb, 50, 0);
  ASSERT(r == 0);
  ASSERT(uv_timer_get_repeat(&repeat_1) == 0);

  /* 设置repeat 1的repeat值 */
  uv_timer_set_repeat(&repeat_1, 50);
  ASSERT(uv_timer_get_repeat(&repeat_1) == 50);

  /* 设置repeat 2，会在repeat 2的回调中重启 */
  r = uv_timer_init(uv_default_loop(), &repeat_2);
  ASSERT(r == 0);
  r = uv_timer_start(&repeat_2, repeat_2_cb, 100, 100);
  ASSERT(r == 0);
  ASSERT(uv_timer_get_repeat(&repeat_2) == 100);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(repeat_1_cb_called == 10);
  ASSERT(repeat_2_cb_called == 2);
  ASSERT(close_cb_called == 2);

  fprintf(stderr, "Test took %ld ms (expected ~700 ms)\n",
          (long int)(uv_now(uv_default_loop()) - start_time));
  fflush(stderr);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
