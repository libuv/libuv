/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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
#include "internal.h"

#include <stddef.h>
#include <unistd.h>

int uv_uptime(double* uptime) {
  char buf[128];
#if !defined(__GNU__)
  static volatile int no_clock_boottime;
  struct timespec now;
  int r;
#endif

  /* Try /proc/uptime first, then fallback to clock_gettime(). */
  
  if (0 == uv__slurp("/proc/uptime", buf, sizeof(buf)))
    if (1 == sscanf(buf, "%lf", uptime))
      return 0;

#if !defined(__GNU__)
  /* Try CLOCK_BOOTTIME first, fall back to CLOCK_MONOTONIC if not available
   * (pre-2.6.39 kernels). CLOCK_MONOTONIC doesn't increase when the system
   * is suspended.
   */
  if (no_clock_boottime) {
    retry_clock_gettime: r = clock_gettime(CLOCK_MONOTONIC, &now);
  }
  else if ((r = clock_gettime(CLOCK_BOOTTIME, &now)) && errno == EINVAL) {
    no_clock_boottime = 1;
    goto retry_clock_gettime;
  }

  if (r)
    return UV__ERR(errno);

  *uptime = now.tv_sec;
  return 0;

#else
  return UV__ERR(EIO);
#endif
}

void uv_loadavg(double avg[3]) {
#if !defined(__GNU__)
  struct sysinfo info;
#endif

  char buf[128];  /* Large enough to hold all of /proc/loadavg. */

  if (0 == uv__slurp("/proc/loadavg", buf, sizeof(buf)))
    if (3 == sscanf(buf, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]))
      return;

#if !defined(__GNU__)
  if (sysinfo(&info) < 0)
    return;

  avg[0] = (double) info.loads[0] / 65536.0;
  avg[1] = (double) info.loads[1] / 65536.0;
  avg[2] = (double) info.loads[2] / 65536.0;
#endif
}
