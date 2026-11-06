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

#ifdef _WIN32

#include "uv.h"
#include "task.h"

#include "../src/win/internal.h"

static void check_ticks(uint64_t ticks, long sec, long usec) {
  FILETIME ft;
  uv_timeval_t tv;

  ft.dwLowDateTime = (DWORD) ticks;
  ft.dwHighDateTime = (DWORD) (ticks >> 32);
  uv__filetime_to_timeval(&ft, &tv);

  ASSERT_EQ(tv.tv_sec, sec);
  ASSERT_EQ(tv.tv_usec, usec);
}

TEST_IMPL(getrusage_filetime_win32) {
  check_ticks(0, 0, 0);
  check_ticks(TICKS_PER_SEC * 86399, 86399, 0);

  /* One day of CPU time or more, e.g. a few hours on a 16 core machine. */
  check_ticks(TICKS_PER_SEC * 86400, 86400, 0);
  check_ticks(TICKS_PER_SEC * 90061 + 5, 90061, 0);
  check_ticks(TICKS_PER_SEC * 31 * 86400 + 10, 31 * 86400, 1);

  /* Sub-millisecond part. */
  check_ticks(1234567, 0, 123456);

  return 0;
}

#else

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif  /* _WIN32 */
