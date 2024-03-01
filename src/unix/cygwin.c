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
#include "internal.h"

#include <sys/sysinfo.h>
#include <unistd.h>

int uv_uptime(double* uptime) {
  struct sysinfo info;

  if (sysinfo(&info) < 0)
    return UV__ERR(errno);

  *uptime = info.uptime;
  return 0;
}

int uv_resident_set_memory(size_t* rss)
{
  char buf[1024];
  const char* s;
  ssize_t n;
  long val;
  int fd;
  int i;
  int e;
  struct sysinfo si;

  do
    fd = open("/proc/self/stat", O_RDONLY);
  while (fd == -1 && errno == EINTR);

  if (fd == -1)
    return UV__ERR(errno);

  do
    n = read(fd, buf, sizeof(buf) - 1);
  while (n == -1 && errno == EINTR);

  uv__close(fd);
  if (n == -1)
    return UV__ERR(errno);
  buf[n] = '\0';

  s = strchr(buf, ' ');
  if (s == NULL)
    goto err;

  s += 1;
  if (*s != '(')
    goto err;

  s = strchr(s, ')');
  if (s == NULL)
    goto err;

  for (i = 1; i <= 22; i++) {
    s = strchr(s + 1, ' ');
    if (s == NULL)
      goto err;
  }

  errno = 0;
  val = strtol(s, NULL, 10);
  if (errno != 0)
    goto err;
  if (val < 0)
    goto err;

  do
    e = sysinfo(&si);
  while (e == -1 && errno == EINTR);
  if (e == -1)
    return UV__ERR(errno);

  *rss = val * si.mem_unit;
  return 0;

err:
  return UV_EINVAL;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  /* FIXME: read /proc/stat? */
  *cpu_infos = NULL;
  *count = 0;
  return UV_ENOSYS;
}

uint64_t uv_get_constrained_memory(void) {
  return 0;  /* Memory constraints are unknown. */
}

uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}
