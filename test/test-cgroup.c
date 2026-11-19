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

#ifdef __linux__

#include "uv.h"
#include "task.h"

#include <string.h>

#define uv__cgroup1_find_cpu_controller uv__test_cgroup1_find_cpu_controller
#include "../src/unix/linux-cgroup.c"

TEST_IMPL(cgroup1_cpu_controller) {
  static const struct {
    const char* cgroup;
    const char* expected;
  } cases[] = {
    {"7:cpu,cpuacct:/docker/123\n", "cpu,cpuacct/docker/123"},
    {"7:cpuacct,cpu:/docker/123\n", "cpuacct,cpu/docker/123"},
    {"7:cpu:/docker/123\n", "cpu/docker/123"},
    {"7:cpu,cpuacct:/\n", "cpu,cpuacct"},
    {"5:memory:/docker/123\n7:cpu,cpuacct:/docker/456\n",
     "cpu,cpuacct/docker/456"},
  };
  char path[256];
  size_t i;

  for (i = 0; i < ARRAY_SIZE(cases); i++) {
    ASSERT_OK(uv__cgroup1_find_cpu_controller(cases[i].cgroup,
                                               path,
                                               sizeof(path)));
    ASSERT_OK(strcmp(path, cases[i].expected));
  }

  ASSERT_EQ(UV_EIO,
            uv__cgroup1_find_cpu_controller("5:memory:/docker/123\n",
                                            path,
                                            sizeof(path)));
  ASSERT_EQ(UV_ENOBUFS,
            uv__cgroup1_find_cpu_controller("7:cpu,cpuacct:/docker/123\n",
                                            path,
                                            sizeof("cpu")));

  return 0;
}

#endif
