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

char cpumask[4 * UV_CPU_SETSIZE];

static void check_affinity(void* arg) {
  char *cpumask = arg;
  uv_thread_t tid = uv_thread_self();
  uv_thread_setaffinity(&tid, cpumask, NULL, UV_CPU_SETSIZE);
  uv_thread_setaffinity(&tid, cpumask + UV_CPU_SETSIZE, cpumask,
                        UV_CPU_SETSIZE);
}

TEST_IMPL(thread_affinity) {
  int t1first, t1second, t2first, t2second;
  uv_thread_t threads[2];

  t1first = UV_CPU_SETSIZE * 0;
  t1second = UV_CPU_SETSIZE * 1;
  t2first = UV_CPU_SETSIZE * 2;
  t2second = UV_CPU_SETSIZE * 3;

  memset(cpumask, 0, 4 * UV_CPU_SETSIZE);

  cpumask[t1first  + 1] = cpumask[t1first  + 3] = 1;
  cpumask[t1second + 0] = cpumask[t1second + 2] = 1;

  cpumask[t2first  + 0] = cpumask[t2first  + 2] = 1;
  cpumask[t2second + 1] = cpumask[t2second + 3] = 1;

  ASSERT(0 == uv_thread_create(threads + 0,
                               check_affinity,
                               &cpumask[t1first]));
  ASSERT(0 == uv_thread_create(threads + 1,
                               check_affinity,
                               &cpumask[t2first]));
  ASSERT(0 == uv_thread_join(threads + 0));
  ASSERT(0 == uv_thread_join(threads + 1));

  ASSERT(0 == cpumask[t1first + 0]);
  ASSERT(1 == cpumask[t1first + 1]);
  ASSERT(0 == cpumask[t1first + 2]);
  ASSERT(1 == cpumask[t1first + 3]);

  ASSERT(1 == cpumask[t2first + 0]);
  ASSERT(0 == cpumask[t2first + 1]);
  ASSERT(1 == cpumask[t2first + 2]);
  ASSERT(0 == cpumask[t2first + 3]);

  return 0;
}
