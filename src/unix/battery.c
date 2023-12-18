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

#if defined(__APPLE__) || TARGET_OS_IPHONE
#include <dlfcn.h>
#include "darwin-stub.h"
#endif  /* defined(__APPLE__) || TARGET_OS_IPHONE */

/* macOS won't need any of this */
#if defined(__linux__)
#include <stdlib.h>
#include "internal.h"
#endif

int uv_battery_info(uv_battery_info_t* info) {
  int err;
  err = UV_ENOSYS;
#if defined(__APPLE__) || TARGET_OS_IPHONE
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

#elif defined(__linux__)
  char buf[1024];
  char path[1024];
  int r;
  int i;
  int battery_count;
  double energy_full;
  double energy_now;
  double power_now;
  struct stat statbuf;

  battery_count = 0;

  // Iterate over all the supported batteries
  for (i = 0; i <= 3; i++) {
    snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d", i);
    r = uv__stat(path, &statbuf);
    // The battery doesn't exist, continue
    if (r < 0)
      continue;

    snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/power_now", i);
    r = uv__slurp(path, buf, sizeof(buf));
    if (r != 0)
      goto out;

    power_now = atof(buf);

    snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/energy_now", i);
    r = uv__slurp(path, buf, sizeof(buf));
    if (r != 0)
      goto out;

    energy_now = atof(buf);

    snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", i);
    r = uv__slurp(path, buf, sizeof(buf));
    if (r != 0)
      goto out;

    if (strstr(buf, "Charging") != NULL) {
      info->is_charging = 1;
      snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/energy_full", i);
      r = uv__slurp(path, buf, sizeof(buf));
      if (r != 0)
        goto out;

      energy_full = atof(buf);

      info->charge_time_in_secs = ((energy_full - energy_now) / power_now) * 3600;
      info->discharge_time_in_secs = 0;
    } else if (strstr(buf, "Discharging") != NULL) {
      info->is_charging = 0;
      info->charge_time_in_secs = 0;
      info->discharge_time_in_secs = (energy_now / power_now) * 3600;
    }

    snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", i);
    r = uv__slurp(path, buf, sizeof(buf));
    if (r != 0)
      goto out;

    info->level = atoi(buf);
    battery_count++;
  }

  if (battery_count == 0) {
    err = UV_ENOSYS;
    goto out2;
  }

  // info->charge_time_in_secs /= battery_count;
  // info->discharge_time_in_secs /= battery_count;

  err = 0;
 out:
  /* In case of error, fill with 0 (sum for merge results later) */
//  info->charge_time_in_secs = 0;
//  info->discharge_time_in_secs = 0;
//  info->is_charging = 0;
//  info->level = 0;

 /* In case of no batteries, leave struct untouched */
 out2:
#endif  /* defined(__APPLE__) || TARGET_OS_IPHONE */
  return err;
}
