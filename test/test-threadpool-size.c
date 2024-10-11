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
  int r;
  unsigned int nthreads;

  /* Can't shrink the threadpool size */
  r = uv_set_threadpool_size(1);
  ASSERT_EQ(r, UV_EINVAL);

  /* The default thread pool size is 4 */
  nthreads = uv_get_threadpool_size();
  ASSERT_EQ(nthreads, 4);

  /* Normal use case (increase the size of the thread pool) */
  r = uv_set_threadpool_size(5);
  ASSERT_OK(r);

  /* The threadpool keeps the same */
  nthreads = uv_get_threadpool_size();
  ASSERT_EQ(nthreads, 5);

  r = uv_set_threadpool_size(6);
  ASSERT_OK(r);
  nthreads = uv_get_threadpool_size();
  ASSERT_EQ(nthreads, 6);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(threadpool_size_env) {
  int r;
  unsigned int nthreads;

#if defined(_WIN32) && defined(__ASAN__)
  /* See investigation in https://github.com/libuv/libuv/issues/4338 */
  RETURN_SKIP("Test does not currently work on Windows under ASAN");
#endif

  r = uv_os_setenv("UV_THREADPOOL_SIZE", "1");
  ASSERT_OK(r);

  /* Can't shrink the threadpool size */
  r = uv_set_threadpool_size(0);
  ASSERT_EQ(r, UV_EINVAL);

  nthreads = uv_get_threadpool_size();
#ifdef _WIN32
  /* juanarbol: why? */
  ASSERT_EQ(nthreads, 4);
#else
  /* The default thread pool size is UV_THREADPOOL_SIZE */
  ASSERT_EQ(nthreads, 1);
#endif


  /* Normal use case (increase the size of the thread pool) */
  r = uv_set_threadpool_size(5);
  ASSERT_OK(r);

  /* The threadpool keeps the same */
  nthreads = uv_get_threadpool_size();
  ASSERT_EQ(nthreads, 5);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
