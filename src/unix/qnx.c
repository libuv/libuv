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

#include <sys/stat.h>

void uv_loadavg(double avg[3]) {

  avg[0] = 0.;
  avg[1] = 0.;
  avg[2] = 0.;
}


int uv_exepath(char* buffer, size_t* size) {
  ssize_t n;
  int fd;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  n = *size - 1;
  if (n > 0) {
    fd = open("/proc/self/exefile", O_RDONLY);
    if(fd !=-1) {
      n = read(fd, buffer, n);
      close(fd);
    }
  }
  if (n == -1 || fd == -1)
    return UV__ERR(errno);

  buffer[n] = '\0';
  *size = n;

  return 0;
}


uint64_t uv_get_free_memory(void) {
  struct stat buf;
  if (stat("/proc", &buf) != -1) { 
    return buf.st_size;
  } else {
    return UV__ERR(errno);
  }
}

uint64_t uv_get_total_memory(void) {
  return 0;
}


uint64_t uv_get_constrained_memory(void) {
  return 0;  /* Memory constraints are unknown. */
}


int uv_resident_set_memory(size_t* rss) {
  return UV_EPERM;
}


int uv_uptime(double* uptime) {
  return UV__ERR(errno);
}


int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  return UV_ENOMEM;
}

int uv__random_sysctl(void* buf, size_t len) {
  return UV_EPERM;
}
