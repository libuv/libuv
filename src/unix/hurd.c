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
#include <mach/vm_statistics.h>

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

uint64_t uv_get_free_memory(void) {
  uint64_t rc;
  struct vm_statistics vmstats;
  
  err = vm_statistics(mach_task_self(), &vmstats);

  if (err)
    return (uint64_t)-1;
  
  return vmstats.free_count * PAGE_SIZE;
}


uint64_t uv_get_total_memory(void) {
  uint64_t rc;
  host_basic_info_data_t hbi;
  mach_msg_type_number_t cnt;
  
  cnt = HOST_BASIC_INFO_COUNT;
  err = host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&hbi, &cnt); 

  if (err)
    return (uint64_t)-1;

  return hbi.memory_size;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  /* FIXME: read /proc/stat? */
  *cpu_infos = NULL;
  *count = 0;
  return UV_ENOSYS;
}
