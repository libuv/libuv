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
#include <TargetConditionals.h>

#if TARGET_OS_IPHONE

int uv__set_process_title(const char* title) {
  return uv__thread_setname(title);
}

#else  /* TARGET_OS_IPHONE */

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "darwin-stub.h"

#define S(s) pCFStringCreateWithCString(NULL, (s), kCFStringEncodingUTF8)

static CFStringRef (*pCFStringCreateWithCString)(CFAllocatorRef,
                                                 const char*,
                                                 CFStringEncoding);
static CFTypeRef (*pLSGetCurrentApplicationASN)(void);
static OSStatus (*pLSSetApplicationInformationItem)(int,
                                                    CFTypeRef,
                                                    CFStringRef,
                                                    CFStringRef,
                                                    CFDictionaryRef*);
static CFStringRef* display_name_key;
static CFDictionaryRef (*pCFBundleGetInfoDictionary)(CFBundleRef);
static CFBundleRef (*pCFBundleGetMainBundle)(void);
static CFDictionaryRef (*pLSApplicationCheckIn)(int, CFDictionaryRef);
static void (*pLSSetApplicationLaunchServicesServerConnectionStatus)(uint64_t,
                                                                     void*);

static void uv__set_process_title_init_once(void) {
  CFBundleRef (*pCFBundleGetBundleWithIdentifier)(CFStringRef);
  CFBundleRef launch_services_bundle;
  void *(*pCFBundleGetDataPointerForName)(CFBundleRef, CFStringRef);
  void *(*pCFBundleGetFunctionPointerForName)(CFBundleRef, CFStringRef);
  void* application_services_handle;
  void* core_foundation_handle;

  application_services_handle = dlopen("/System/Library/Frameworks/"
                                       "ApplicationServices.framework/"
                                       "Versions/A/ApplicationServices",
                                       RTLD_LAZY | RTLD_LOCAL);
  if (application_services_handle == NULL)
    return;
  core_foundation_handle = dlopen("/System/Library/Frameworks/"
                                  "CoreFoundation.framework/"
                                  "Versions/A/CoreFoundation",
                                  RTLD_LAZY | RTLD_LOCAL);
  if (core_foundation_handle == NULL)
    return;

  *(void **)(&pCFStringCreateWithCString) =
      dlsym(core_foundation_handle, "CFStringCreateWithCString");
  *(void **)(&pCFBundleGetBundleWithIdentifier) =
      dlsym(core_foundation_handle, "CFBundleGetBundleWithIdentifier");
  *(void **)(&pCFBundleGetDataPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetDataPointerForName");
  *(void **)(&pCFBundleGetFunctionPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetFunctionPointerForName");

  if (pCFStringCreateWithCString == NULL ||
      pCFBundleGetBundleWithIdentifier == NULL ||
      pCFBundleGetDataPointerForName == NULL ||
      pCFBundleGetFunctionPointerForName == NULL) {
    return;
  }

  launch_services_bundle =
      pCFBundleGetBundleWithIdentifier(S("com.apple.LaunchServices"));

  if (launch_services_bundle == NULL)
    return;

  *(void **)(&pLSGetCurrentApplicationASN) =
      pCFBundleGetFunctionPointerForName(launch_services_bundle,
                                         S("_LSGetCurrentApplicationASN"));

  if (pLSGetCurrentApplicationASN == NULL)
    return;

  *(void **)(&pLSSetApplicationInformationItem) =
      pCFBundleGetFunctionPointerForName(launch_services_bundle,
                                         S("_LSSetApplicationInformationItem"));

  if (pLSSetApplicationInformationItem == NULL)
    return;

  display_name_key = pCFBundleGetDataPointerForName(launch_services_bundle,
                                                    S("_kLSDisplayNameKey"));

  if (display_name_key == NULL || *display_name_key == NULL)
    return;

  *(void **)(&pCFBundleGetInfoDictionary) = dlsym(core_foundation_handle,
                                     "CFBundleGetInfoDictionary");
  *(void **)(&pCFBundleGetMainBundle) = dlsym(core_foundation_handle,
                                 "CFBundleGetMainBundle");
  if (pCFBundleGetInfoDictionary == NULL || pCFBundleGetMainBundle == NULL)
    return;

  *(void **)(&pLSApplicationCheckIn) = pCFBundleGetFunctionPointerForName(
      launch_services_bundle,
      S("_LSApplicationCheckIn"));

  if (pLSApplicationCheckIn == NULL)
    return;

  *(void **)(&pLSSetApplicationLaunchServicesServerConnectionStatus) =
      pCFBundleGetFunctionPointerForName(
          launch_services_bundle,
          S("_LSSetApplicationLaunchServicesServerConnectionStatus"));
}

int uv__set_process_title(const char* title) {
  static uv_once_t once;
  CFTypeRef asn;

  uv__thread_setname(title);  /* Don't care if it fails. */

  uv_once(&once, uv__set_process_title_init_once);

  if (pLSSetApplicationLaunchServicesServerConnectionStatus == NULL)
    return 0;

  pLSSetApplicationLaunchServicesServerConnectionStatus(0, NULL);

  /* Check into process manager?! */
  pLSApplicationCheckIn(-2,
                        pCFBundleGetInfoDictionary(pCFBundleGetMainBundle()));

  asn = pLSGetCurrentApplicationASN();
  if (asn == NULL)
    return UV_EBUSY;

  if (pLSSetApplicationInformationItem(-2,  /* Magic value. */
                                       asn,
                                       *display_name_key,
                                       S(title),
                                       NULL) != noErr) {
    return UV_EINVAL;
  }

  return 0;
}

#endif  /* TARGET_OS_IPHONE */
