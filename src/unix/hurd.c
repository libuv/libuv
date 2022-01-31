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

#define _GNU_SOURCE 1

#include "uv.h"
#include "internal.h"

#include <hurd.h>
#include <hurd/process.h>
#include <mach/task_info.h>

#include <inttypes.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

int uv_exepath(char* buffer, size_t* size) {
  kern_return_t err;
  string_t buf;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  if (*size - 1 > 0) {
    err = proc_get_exe(getproc(), getpid(), buf);

    if (err)
      return UV__ERR(err);
  }    

  *size = strncpy(buffer, buf, *size) - buffer;

  return 0;  
}

int uv_resident_set_memory(size_t* rss) {
  kern_return_t err;
  struct task_basic_info bi;
  mach_msg_type_number_t count;

  count = TASK_BASIC_INFO_COUNT;
  err = task_info(mach_task_self(), TASK_BASIC_INFO,
		  (task_info_t)&bi, &count);

  if (err)
    return UV__ERR(err);

  *rss = bi.resident_size;

  return 0;
}

static int uv__slurp(const char* filename, char* buf, size_t len) {
  ssize_t n;
  int fd;

  assert(len > 0);

  fd = uv__open_cloexec(filename, O_RDONLY);
  if (fd < 0)
    return fd;

  do
    n = read(fd, buf, len - 1);
  while (n == -1 && errno == EINTR);

  if (uv__close_nocheckstdio(fd))
    abort();

  if (n < 0)
    return UV__ERR(errno);

  buf[n] = '\0';

  return 0;
}

static uint64_t uv__read_proc_meminfo(const char* what) {
  uint64_t rc;
  char* p;
  char buf[4096];  /* Large enough to hold all of /proc/meminfo. */

  if (uv__slurp("/proc/meminfo", buf, sizeof(buf)))
    return 0;

  p = strstr(buf, what);

  if (p == NULL)
    return 0;

  p += strlen(what);

  rc = 0;
  sscanf(p, "%" PRIu64 " kB", &rc);

  return rc * 1024;
}


uint64_t uv_get_free_memory(void) {
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemAvailable:");

  return rc;
}


uint64_t uv_get_total_memory(void) {
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemTotal:");

  return rc;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  /* FIXME: read /proc/stat? */
  *cpu_infos = NULL;
  *count = 0;
  return UV_ENOSYS;
}
