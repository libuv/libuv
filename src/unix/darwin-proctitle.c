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

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <TargetConditionals.h>

#if !TARGET_OS_IPHONE
#include "darwin-stub.h"

static int use_launch_services = 1;

static struct {
  CFStringRef (*CFStringCreateWithCString)(CFAllocatorRef,
                                           const char*,
                                           CFStringEncoding);
  void (*CFRelease)(CFTypeRef);
  CFDictionaryRef (*CFBundleGetInfoDictionary)(CFBundleRef);
  CFBundleRef (*CFBundleGetMainBundle)(void);
  CFTypeRef (*LSGetCurrentApplicationASN)(void);
  OSStatus (*LSSetApplicationInformationItem)(int,
                                              CFTypeRef,
                                              CFStringRef,
                                              CFStringRef,
                                              CFDictionaryRef*);
  CFDictionaryRef (*LSApplicationCheckIn)(int, CFDictionaryRef);
  void (*LSSetApplicationLaunchServicesServerConnectionStatus)(uint64_t,
                                                               void*);
  CFStringRef display_name_key;
} ls;


static int uv__ls_lookup(CFBundleRef bundle,
                         void* (*lookup)(CFBundleRef, CFStringRef),
                         const char* name,
                         void** p) {
  CFStringRef string;

  string = ls.CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
  if (string == NULL)
    return UV_ENOMEM;

  *p = lookup(bundle, string);
  ls.CFRelease(string);

  return 0;
}


static int uv__process_title_lookup(void) {
  CFBundleRef (*pCFBundleGetBundleWithIdentifier)(CFStringRef);
  void* (*pCFBundleGetDataPointerForName)(CFBundleRef, CFStringRef);
  void* (*pCFBundleGetFunctionPointerForName)(CFBundleRef, CFStringRef);
  void* application_services_handle;
  void* core_foundation_handle;
  CFBundleRef launch_services_bundle;
  CFStringRef* display_name_key;
  CFStringRef string;
  int err;

  err = UV_ENOENT;

  /* ApplicationServices is not used directly but loading it ensures that
   * the LaunchServices bundle is available.
   */
  application_services_handle = dlopen("/System/Library/Frameworks/"
                                       "ApplicationServices.framework/"
                                       "Versions/A/ApplicationServices",
                                       RTLD_LAZY | RTLD_LOCAL);
  core_foundation_handle = dlopen("/System/Library/Frameworks/"
                                  "CoreFoundation.framework/"
                                  "Versions/A/CoreFoundation",
                                  RTLD_LAZY | RTLD_LOCAL);

  if (application_services_handle == NULL || core_foundation_handle == NULL)
    goto out;

  *(void **)(&ls.CFStringCreateWithCString) =
      dlsym(core_foundation_handle, "CFStringCreateWithCString");
  *(void **)(&ls.CFRelease) = dlsym(core_foundation_handle, "CFRelease");
  *(void **)(&ls.CFBundleGetInfoDictionary) =
      dlsym(core_foundation_handle, "CFBundleGetInfoDictionary");
  *(void **)(&ls.CFBundleGetMainBundle) =
      dlsym(core_foundation_handle, "CFBundleGetMainBundle");
  *(void **)(&pCFBundleGetBundleWithIdentifier) =
      dlsym(core_foundation_handle, "CFBundleGetBundleWithIdentifier");
  *(void **)(&pCFBundleGetDataPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetDataPointerForName");
  *(void **)(&pCFBundleGetFunctionPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetFunctionPointerForName");

  if (ls.CFStringCreateWithCString == NULL ||
      ls.CFRelease == NULL ||
      ls.CFBundleGetInfoDictionary == NULL ||
      ls.CFBundleGetMainBundle == NULL ||
      pCFBundleGetBundleWithIdentifier == NULL ||
      pCFBundleGetDataPointerForName == NULL ||
      pCFBundleGetFunctionPointerForName == NULL) {
    goto out;
  }

  string = ls.CFStringCreateWithCString(NULL,
                                        "com.apple.LaunchServices",
                                        kCFStringEncodingUTF8);
  if (string == NULL) {
    err = UV_ENOMEM;
    goto out;
  }

  launch_services_bundle = pCFBundleGetBundleWithIdentifier(string);
  ls.CFRelease(string);

  if (launch_services_bundle == NULL)
    goto out;

#define F(lookup, name, p)                                                    \
  do {                                                                        \
    err = uv__ls_lookup(launch_services_bundle, (lookup), (name), (p));       \
    if (err != 0)                                                             \
      goto out;                                                               \
    err = UV_ENOENT;                                                          \
    if (*(p) == NULL)                                                         \
      goto out;                                                               \
  } while (0)

  F(pCFBundleGetFunctionPointerForName,
    "_LSGetCurrentApplicationASN",
    (void**) &ls.LSGetCurrentApplicationASN);
  F(pCFBundleGetFunctionPointerForName,
    "_LSSetApplicationInformationItem",
    (void**) &ls.LSSetApplicationInformationItem);
  F(pCFBundleGetDataPointerForName,
    "_kLSDisplayNameKey",
    (void**) &display_name_key);
  F(pCFBundleGetFunctionPointerForName,
    "_LSApplicationCheckIn",
    (void**) &ls.LSApplicationCheckIn);
  F(pCFBundleGetFunctionPointerForName,
    "_LSSetApplicationLaunchServicesServerConnectionStatus",
    (void**) &ls.LSSetApplicationLaunchServicesServerConnectionStatus);
#undef F

  if (*display_name_key == NULL)
    goto out;

  ls.display_name_key = *display_name_key;
  return 0;

out:
  if (core_foundation_handle != NULL)
    dlclose(core_foundation_handle);

  if (application_services_handle != NULL)
    dlclose(application_services_handle);

  return err;
}
#endif  /* !TARGET_OS_IPHONE */


void uv__process_title_init(void) {
#if !TARGET_OS_IPHONE
  const char* val;

  /* Read the environment at startup, not during concurrent title updates. */
  val = getenv("UV_PROCESS_TITLE_USE_LAUNCH_SERVICES");
  use_launch_services = val == NULL || strcmp(val, "0") != 0;
#endif  /* !TARGET_OS_IPHONE */
}


int uv__set_process_title(const char* title) {
#if TARGET_OS_IPHONE
  return uv__thread_setname(title);
#else
  CFTypeRef asn;
  CFDictionaryRef info;
  CFStringRef string;
  OSStatus status;
  int err;

  if (!use_launch_services)
    return uv__thread_setname(title);

  if (ls.CFRelease == NULL) {
    err = uv__process_title_lookup();
    if (err != 0)
      return err;
  }

  ls.LSSetApplicationLaunchServicesServerConnectionStatus(0, NULL);
  info = ls.CFBundleGetInfoDictionary(ls.CFBundleGetMainBundle());
  ls.LSApplicationCheckIn(/*magic value*/-2, info);

  asn = ls.LSGetCurrentApplicationASN();
  if (asn == NULL)
    return UV_EBUSY;

  string = ls.CFStringCreateWithCString(NULL, title, kCFStringEncodingUTF8);
  if (string == NULL)
    return UV_ENOMEM;

  status = ls.LSSetApplicationInformationItem(-2,  /* Magic value. */
                                              asn,
                                              ls.display_name_key,
                                              string,
                                              NULL);
  ls.CFRelease(string);

  if (status != noErr)
    return UV_EINVAL;

  uv__thread_setname(title);  /* Don't care if it fails. */

  return 0;
#endif  /* !TARGET_OS_IPHONE */
}
