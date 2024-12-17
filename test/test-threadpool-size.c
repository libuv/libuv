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

TEST_IMPL(threadpool_size) {
  /* The default thread pool size is 4 */
  ASSERT_EQ(uv_get_threadpool_size(), 4);

  /* Normal use case (increase the size of the thread pool) */
  ASSERT_OK(uv_set_threadpool_size(5));
  ASSERT_EQ(uv_get_threadpool_size(), 5);

  /* Shrink the thread pool */
  ASSERT_OK(uv_set_threadpool_size(4));
  ASSERT_EQ(uv_get_threadpool_size(), 4);

  /* Grow the thread pool again */
  ASSERT_OK(uv_set_threadpool_size(5));
  ASSERT_EQ(uv_get_threadpool_size(), 5);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
