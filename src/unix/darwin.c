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
#include "darwin-stub.h"

#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <dlfcn.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>  /* sysconf */

static uv_once_t once = UV_ONCE_INIT;
static mach_timebase_info_data_t timebase;


int uv__platform_loop_init(uv_loop_t* loop) {
  loop->cf_state = NULL;

  if (uv__kqueue_init(loop))
    return UV__ERR(errno);

  return 0;
}


void uv__platform_loop_delete(uv_loop_t* loop) {
  uv__fsevents_loop_delete(loop);
}


static void uv__hrtime_init_once(void) {
  if (KERN_SUCCESS != mach_timebase_info(&timebase))
    abort();
}


uint64_t uv__hrtime(uv_clocktype_t type) {
  uv_once(&once, uv__hrtime_init_once);
  return mach_continuous_time() * timebase.numer / timebase.denom;
}


int uv_exepath(char* buffer, size_t* size) {
  /* realpath(exepath) may be > PATH_MAX so double it to be on the safe side. */
  char abspath[PATH_MAX * 2 + 1];
  char exepath[PATH_MAX + 1];
  uint32_t exepath_size;
  size_t abspath_size;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  exepath_size = sizeof(exepath);
  if (_NSGetExecutablePath(exepath, &exepath_size))
    return UV_EIO;

  if (realpath(exepath, abspath) != abspath)
    return UV__ERR(errno);

  abspath_size = strlen(abspath);
  if (abspath_size == 0)
    return UV_EIO;

  *size -= 1;
  if (*size > abspath_size)
    *size = abspath_size;

  memcpy(buffer, abspath, *size);
  buffer[*size] = '\0';

  return 0;
}


uint64_t uv_get_free_memory(void) {
  vm_statistics_data_t info;
  mach_msg_type_number_t count = sizeof(info) / sizeof(integer_t);

  if (host_statistics(mach_host_self(), HOST_VM_INFO,
                      (host_info_t)&info, &count) != KERN_SUCCESS) {
    return 0;
  }

  return (uint64_t) info.free_count * sysconf(_SC_PAGESIZE);
}


uint64_t uv_get_total_memory(void) {
  uint64_t info;
  int which[] = {CTL_HW, HW_MEMSIZE};
  size_t size = sizeof(info);

  if (sysctl(which, ARRAY_SIZE(which), &info, &size, NULL, 0))
    return 0;

  return (uint64_t) info;
}


uint64_t uv_get_constrained_memory(void) {
  return 0;  /* Memory constraints are unknown. */
}


uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}


void uv_loadavg(double avg[3]) {
  struct loadavg info;
  size_t size = sizeof(info);
  int which[] = {CTL_VM, VM_LOADAVG};

  if (sysctl(which, ARRAY_SIZE(which), &info, &size, NULL, 0) < 0) return;

  avg[0] = (double) info.ldavg[0] / info.fscale;
  avg[1] = (double) info.ldavg[1] / info.fscale;
  avg[2] = (double) info.ldavg[2] / info.fscale;
}


int uv_resident_set_memory(size_t* rss) {
  mach_msg_type_number_t count;
  task_basic_info_data_t info;
  kern_return_t err;

  count = TASK_BASIC_INFO_COUNT;
  err = task_info(mach_task_self(),
                  TASK_BASIC_INFO,
                  (task_info_t) &info,
                  &count);
  (void) &err;
  /* task_info(TASK_BASIC_INFO) cannot really fail. Anything other than
   * KERN_SUCCESS implies a libuv bug.
   */
  assert(err == KERN_SUCCESS);
  *rss = info.resident_size;

  return 0;
}


int uv_uptime(double* uptime) {
  time_t now;
  struct timeval info;
  size_t size = sizeof(info);
  static int which[] = {CTL_KERN, KERN_BOOTTIME};

  if (sysctl(which, ARRAY_SIZE(which), &info, &size, NULL, 0))
    return UV__ERR(errno);

  now = time(NULL);
  *uptime = now - info.tv_sec;

  return 0;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  unsigned int ticks = (unsigned int)sysconf(_SC_CLK_TCK),
               multiplier = ((uint64_t)1000L / ticks);
  char model[512];
  uint64_t cpuspeed;
  size_t size;
  unsigned int i;
  natural_t numcpus;
  mach_msg_type_number_t msg_type;
  processor_cpu_load_info_data_t *info;
  uv_cpu_info_t* cpu_info;

  size = sizeof(model);
  if (sysctlbyname("machdep.cpu.brand_string", &model, &size, NULL, 0) &&
      sysctlbyname("hw.model", &model, &size, NULL, 0)) {
    return UV__ERR(errno);
  }

  cpuspeed = 0;
  size = sizeof(cpuspeed);
  sysctlbyname("hw.cpufrequency", &cpuspeed, &size, NULL, 0);
  if (cpuspeed == 0)
    /* If sysctl hw.cputype == CPU_TYPE_ARM64, the correct value is unavailable
     * from Apple, but we can hard-code it here to a plausible value. */
    cpuspeed = 2400000000U;

  if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numcpus,
                          (processor_info_array_t*)&info,
                          &msg_type) != KERN_SUCCESS) {
    return UV_EINVAL;  /* FIXME(bnoordhuis) Translate error. */
  }

  *cpu_infos = uv__malloc(numcpus * sizeof(**cpu_infos));
  if (!(*cpu_infos)) {
    vm_deallocate(mach_task_self(), (vm_address_t)info, msg_type);
    return UV_ENOMEM;
  }

  *count = numcpus;

  for (i = 0; i < numcpus; i++) {
    cpu_info = &(*cpu_infos)[i];

    cpu_info->cpu_times.user = (uint64_t)(info[i].cpu_ticks[0]) * multiplier;
    cpu_info->cpu_times.nice = (uint64_t)(info[i].cpu_ticks[3]) * multiplier;
    cpu_info->cpu_times.sys = (uint64_t)(info[i].cpu_ticks[1]) * multiplier;
    cpu_info->cpu_times.idle = (uint64_t)(info[i].cpu_ticks[2]) * multiplier;
    cpu_info->cpu_times.irq = 0;

    cpu_info->model = uv__strdup(model);
    cpu_info->speed = (int)(cpuspeed / 1000000);
  }
  vm_deallocate(mach_task_self(), (vm_address_t)info, msg_type);

  return 0;
}


int uv_battery_info(uv_battery_info_t* info) {
  int err;
  CFTypeRef (*pIOPSCopyPowerSourcesInfo)(void);
  CFArrayRef (*pIOPSCopyPowerSourcesList)(CFTypeRef);
  CFIndex (*pCFArrayGetCount)(CFArrayRef);
  CFArrayRef (*pIOPSGetPowerSourceDescription)(CFTypeRef, CFTypeRef);
  void (*pCFRelease)(CFTypeRef);
  void (*pCFNumberGetValue)(CFNumberRef, int, void *);
  void* (*pCFArrayGetValueAtIndex)(CFArrayRef, CFIndex);
  void* (*pCFDictionaryGetValue)(CFDictionaryRef, const void*);
  int (*pCFBooleanGetValue)(CFBooleanRef);
  CFStringRef (*pCFStringCreateWithCString)(CFAllocatorRef,
                                            const char*,
                                            CFStringEncoding);

  CFBooleanRef is_charging_value;
  CFDictionaryRef power_source;
  CFNumberRef current_capacity;
  CFNumberRef time_remaining;
  CFArrayRef sources = NULL;
  CFTypeRef blob = NULL;
  int h = 0;
  int is_charging = 0;
  void* core_foundation_handle;
  void* iokit_framekwork_handle;

  err = UV_ENOENT;
  iokit_framekwork_handle = dlopen("/System/Library/Frameworks/"
                                   "IOKit.framework/IOKit",
                                   RTLD_LAZY | RTLD_LOCAL);

  core_foundation_handle = dlopen("/System/Library/Frameworks/"
                                  "CoreFoundation.framework/CoreFoundation",
                                  RTLD_LAZY | RTLD_LOCAL);

  if (iokit_framekwork_handle == NULL || core_foundation_handle == NULL)
    goto out;

  *(void **)(&pCFRelease) = dlsym(core_foundation_handle, "CFRelease");
  *(void **)(&pCFStringCreateWithCString) =
      dlsym(core_foundation_handle, "CFStringCreateWithCString");
  *(void **)(&pCFArrayGetCount) =
      dlsym(core_foundation_handle, "CFArrayGetCount");
  *(void **)(&pCFArrayGetValueAtIndex) =
      dlsym(core_foundation_handle, "CFArrayGetValueAtIndex");
  *(void **)(&pCFNumberGetValue) =
      dlsym(core_foundation_handle, "CFNumberGetValue");
  *(void **)(&pCFBooleanGetValue) =
      dlsym(core_foundation_handle, "CFBooleanGetValue");
  *(void **)(&pCFDictionaryGetValue) =
      dlsym(core_foundation_handle, "CFDictionaryGetValue");
  *(void **)(&pIOPSCopyPowerSourcesInfo) =
      dlsym(iokit_framekwork_handle, "IOPSCopyPowerSourcesInfo");
  *(void **)(&pIOPSCopyPowerSourcesList) =
      dlsym(iokit_framekwork_handle, "IOPSCopyPowerSourcesList");
  *(void **)(&pIOPSGetPowerSourceDescription) =
      dlsym(iokit_framekwork_handle, "IOPSGetPowerSourceDescription");

  if (pCFRelease == NULL ||
      pCFStringCreateWithCString == NULL ||
      pCFArrayGetCount == NULL ||
      pCFArrayGetValueAtIndex == NULL ||
      pCFNumberGetValue == NULL ||
      pCFDictionaryGetValue == NULL ||
      pIOPSCopyPowerSourcesInfo == NULL ||
      pIOPSCopyPowerSourcesList == NULL ||
      pIOPSGetPowerSourceDescription == NULL) {
    goto out;
  }

#define S(s) pCFStringCreateWithCString(NULL, (s), kCFStringEncodingUTF8)

  blob = pIOPSCopyPowerSourcesInfo();
  if (blob == NULL)
    goto out;

  sources = pIOPSCopyPowerSourcesList(blob);
  if (sources == NULL)
    goto out;

  /* If there are no sources, we're on a desktop or server, nothing to do */
  if (pCFArrayGetCount(sources) == 0) {
    err = UV_ENOSYS;
    goto out;
  }

  power_source =
    pIOPSGetPowerSourceDescription(blob, pCFArrayGetValueAtIndex(sources, 0));
  if (power_source == NULL)
    goto out;

  err = 0;

  /* kIOPSCurrentCapacityKey */
  current_capacity = pCFDictionaryGetValue(power_source, S("Current Capacity"));
  if (current_capacity != NULL) {
    pCFNumberGetValue(current_capacity, pkCFNumberIntType, &h);
    info->level = h;
  }

  /* Retrieve is_charging status kIOPSIsChargingKey */
  is_charging_value = pCFDictionaryGetValue(power_source, S("Is Charging"));
  if (is_charging_value != NULL) {
    is_charging = pCFBooleanGetValue(is_charging_value);
    info->is_charging = is_charging;
  }

  /* kIOPSTimeToFullChargeKey and kIOPSTimeToEmptyKey */
  time_remaining =
    pCFDictionaryGetValue(power_source,
        is_charging ? S("Time to Full Charge") : S("Time to Empty"));
  if (time_remaining != NULL) {
    pCFNumberGetValue(time_remaining, pkCFNumberIntType, &h);
  }

  /* A value of -1 indicates "Still Calculating the Time" */
  if (h == -1)
    h = 0;

  /* Default unit is minutes, convert to seconds */
  h = h * 60;
  if (is_charging == 1) {
    info->charge_time_in_secs = h;
    info->discharge_time_in_secs = 0;
  } else {
    info->discharge_time_in_secs = h;
    info->charge_time_in_secs = 0;
  }

 out:
  if (iokit_framekwork_handle != NULL)
    dlclose(iokit_framekwork_handle);

  if (core_foundation_handle != NULL)
    dlclose(core_foundation_handle);

  if (blob != NULL)
    pCFRelease(blob);

  if (sources != NULL)
    pCFRelease(sources);

  return err;
}
