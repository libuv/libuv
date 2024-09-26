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

#include <dlfcn.h>

#if !TARGET_OS_IPHONE
#include "darwin-stub.h"
#endif

int uv_battery_info(uv_battery_info_t* info) {
  CFTimeInterval (*pIOPSGetTimeRemainingEstimate)(void);
  void* iokit_framekwork_handle;
  int err;

  err = UV_ENOENT;
  iokit_framekwork_handle = dlopen("/System/Library/Frameworks/"
                                  "IOKit.framework/IOKit",
                                  RTLD_LAZY | RTLD_LOCAL);

  if (iokit_framekwork_handle == NULL)
    goto out;

  *(void **)(&pIOPSGetTimeRemainingEstimate) =
      dlsym(iokit_framekwork_handle, "IOPSGetTimeRemainingEstimate");

  if (pIOPSGetTimeRemainingEstimate == NULL)
    goto out;

  err = UV_EINVAL;
  info->charging = pIOPSGetTimeRemainingEstimate();

  // -1 kIOPSTimeRemainingUnknown
  // -2 kIOPSTimeRemainingUnlimited
  // Anything else the estimated minutes remaining until all power sources
  // (battery and/or UPS's) are empty
  // Convert minutes into seconds
  if (info->charging > 0) info->charging *= 60;

  err = 0;

out:
  if (iokit_framekwork_handle != NULL)
    dlclose(iokit_framekwork_handle);

  return err;
}
