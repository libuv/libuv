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

#ifndef MILLISEC
# define MILLISEC 1000
#endif

#ifndef NANOSEC
# define NANOSEC ((uint64_t) 1e9)
#endif


TEST_IMPL(hrtime) {
  uint64_t a, b, diff;
  /* 50 iterations * 300 ms max (assert-enforced) = 15 seconds. */
  int i = 50;
  while (i > 0) {
    a = uv_hrtime();
    uv_sleep(45);
    b = uv_hrtime();

    diff = b - a;

    /* printf("i= %d diff = %llu\n", i, (unsigned long long int) diff); */

    /* Be generous with assertions about the difference between the
     * two hrtime values.
     * - The Windows Sleep() function has a resolution of 10-20 ms.
     * - On MacOSX 12 CI we observed diff values between (48, 195) ms. */
    ASSERT(diff > (uint64_t) 20 * NANOSEC / MILLISEC);
    ASSERT(diff < (uint64_t) 300 * NANOSEC / MILLISEC);
    --i;
  }
  return 0;
}
