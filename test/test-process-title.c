/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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
#include "task.h"
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif


static void set_title(const char* title) {
  char buffer[512];
  int err;

  err = uv_get_process_title(buffer, sizeof(buffer));
  ASSERT_OK(err);

  err = uv_set_process_title(title);
  ASSERT_OK(err);

  err = uv_get_process_title(buffer, sizeof(buffer));
  ASSERT_OK(err);

  ASSERT_OK(strcmp(buffer, title));
}


static void uv_get_process_title_edge_cases(void) {
  char buffer[512];
  int r;

  /* Test a NULL buffer */
  r = uv_get_process_title(NULL, 100);
  ASSERT_EQ(r, UV_EINVAL);

  /* Test size of zero */
  r = uv_get_process_title(buffer, 0);
  ASSERT_EQ(r, UV_EINVAL);

  /* Test for insufficient buffer size */
  r = uv_get_process_title(buffer, 1);
  ASSERT_EQ(r, UV_ENOBUFS);
}


TEST_IMPL(process_title) {
#if defined(__sun) || defined(__CYGWIN__) || defined(__MSYS__) || \
    defined(__PASE__) || defined(__QNX__)
  RETURN_SKIP("uv_(get|set)_process_title is not implemented.");
#endif

  /* Check for format string vulnerabilities. */
  set_title("%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s");
  set_title("new title");

  /* Check uv_get_process_title() edge cases */
  uv_get_process_title_edge_cases();

  return 0;
}


static void exit_cb(uv_process_t* process, int64_t status, int signo) {
  ASSERT_OK(status);
  ASSERT_OK(signo);
  uv_close((uv_handle_t*) process, NULL);
}


TEST_IMPL(process_title_big_argv) {
  uv_process_options_t options;
  uv_process_t process;
  size_t exepath_size;
  char exepath[1024];
  char jumbo[512];
  char* args[5];

#ifdef _WIN32
  /* Remove once https://github.com/libuv/libuv/issues/2667 is fixed. */
  uv_set_process_title("run-tests");
#endif

  exepath_size = sizeof(exepath) - 1;
  ASSERT_OK(uv_exepath(exepath, &exepath_size));
  exepath[exepath_size] = '\0';

  memset(jumbo, 'x', sizeof(jumbo) - 1);
  jumbo[sizeof(jumbo) - 1] = '\0';

  /* Note: need to pass three arguments, not two, otherwise
   * run-tests.c thinks it's the name of a test to run.
   */
  args[0] = exepath;
  args[1] = "process_title_big_argv_helper";
  args[2] = jumbo;
  args[3] = jumbo;
  args[4] = NULL;

  memset(&options, 0, sizeof(options));
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/* Called by process_title_big_argv_helper. */
void process_title_big_argv(void) {
  char buf[256] = "fail";

  /* Return value deliberately ignored. */
  uv_get_process_title(buf, sizeof(buf));
  ASSERT_NE(0, strcmp(buf, "fail"));
}


#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <dlfcn.h>
#include "../src/unix/darwin-stub.h"

static CFStringRef (*cf_string_create)(CFAllocatorRef,
                                      const char*,
                                      CFStringEncoding);
static void (*cf_release)(CFTypeRef);
static void* (*cf_bundle_get_function_pointer_for_name)(CFBundleRef,
                                                        CFStringRef);
static CFStringRef cf_strings[7];
static char cf_string_names[7][128];
static unsigned int cf_create_calls;
static unsigned int cf_string_count;
static unsigned int cf_release_count;
static unsigned int cf_fail_at;
static unsigned int ls_get_asn_count;
static unsigned int ls_checkin_count;
static unsigned int ls_set_info_count;
static int ls_asn_available;
static OSStatus ls_set_info_status;
static char* process_title_env_value;
static const unsigned int ls_call_get_asn = 1;
static const unsigned int ls_call_set_connection_status = 2;
static const unsigned int ls_call_check_in = 3;
static const unsigned int ls_call_set_info = 4;
static unsigned int ls_call_order[5];
static unsigned int ls_call_order_count;
static int ls_checkin_tag;
static uint64_t ls_connection_status;
static void* ls_connection;
static int ls_set_info_tag;
static CFTypeRef ls_set_info_asn;
static int ls_test_asn;
static int ls_asn_available_after_checkin;


static void record_ls_call(unsigned int call) {
  if (ls_call_order_count < ARRAY_SIZE(ls_call_order))
    ls_call_order[ls_call_order_count] = call;
  ls_call_order_count++;
}


static char* tracked_getenv(const char* name) {
  if (strcmp(name, "UV_PROCESS_TITLE_USE_LAUNCH_SERVICES") == 0)
    return process_title_env_value;
  return getenv(name);
}


static CFTypeRef tracked_ls_get_current_application_asn(void) {
  record_ls_call(ls_call_get_asn);
  ls_get_asn_count++;
  return ls_asn_available ? (CFTypeRef) &ls_test_asn : NULL;
}


static CFDictionaryRef tracked_ls_application_check_in(int asn,
                                                        CFDictionaryRef info) {
  record_ls_call(ls_call_check_in);
  ls_checkin_tag = asn;
  (void) info;
  ls_checkin_count++;
  ls_asn_available = ls_asn_available_after_checkin;
  return NULL;
}


static OSStatus tracked_ls_set_application_information_item(
    int tag,
    CFTypeRef asn,
    CFStringRef key,
    CFStringRef value,
    CFDictionaryRef* dict) {
  record_ls_call(ls_call_set_info);
  ls_set_info_tag = tag;
  ls_set_info_asn = asn;
  (void) key;
  (void) value;
  (void) dict;
  ls_set_info_count++;
  return ls_set_info_status;
}


static void tracked_ls_set_connection_status(uint64_t status,
                                             void* connection) {
  record_ls_call(ls_call_set_connection_status);
  ls_connection_status = status;
  ls_connection = connection;
}


static CFStringRef tracked_cf_string_create(CFAllocatorRef allocator,
                                            const char* string,
                                            CFStringEncoding encoding) {
  CFStringRef result;

  if (++cf_create_calls == cf_fail_at)
    return NULL;

  result = cf_string_create(allocator, string, encoding);
  ASSERT_NOT_NULL(result);
  ASSERT_LT(cf_string_count, ARRAY_SIZE(cf_strings));
  strncpy(cf_string_names[cf_string_count], string,
          sizeof(cf_string_names[cf_string_count]) - 1);
  cf_string_names[cf_string_count][sizeof(cf_string_names[0]) - 1] = '\0';
  cf_strings[cf_string_count++] = result;
  return result;
}


static void tracked_cf_release(CFTypeRef object) {
  unsigned int i;

  ASSERT_NOT_NULL(object);
  for (i = 0; i < cf_string_count; i++)
    if (cf_strings[i] == object)
      break;
  ASSERT_LT(i, cf_string_count);
  cf_strings[i] = NULL;
  cf_release_count++;
  cf_release(object);
}


static void* tracked_cf_bundle_get_function_pointer_for_name(
    CFBundleRef bundle,
    CFStringRef name) {
  unsigned int i;

  for (i = 0; i < cf_string_count; i++) {
    if (cf_strings[i] != name)
      continue;
    if (strcmp(cf_string_names[i], "_LSGetCurrentApplicationASN") == 0)
      return (void*) tracked_ls_get_current_application_asn;
    if (strcmp(cf_string_names[i], "_LSApplicationCheckIn") == 0)
      return (void*) tracked_ls_application_check_in;
    if (strcmp(cf_string_names[i], "_LSSetApplicationInformationItem") == 0)
      return (void*) tracked_ls_set_application_information_item;
    if (strcmp(cf_string_names[i],
               "_LSSetApplicationLaunchServicesServerConnectionStatus") == 0)
      return (void*) tracked_ls_set_connection_status;
  }

  return cf_bundle_get_function_pointer_for_name(bundle, name);
}


static void* tracked_dlsym(void* handle, const char* symbol) {
  void* result;

  result = dlsym(handle, symbol);
  if (result == NULL)
    return NULL;
  if (strcmp(symbol, "CFStringCreateWithCString") == 0) {
    *(void**) &cf_string_create = result;
    return (void*) tracked_cf_string_create;
  }
  if (strcmp(symbol, "CFRelease") == 0) {
    *(void**) &cf_release = result;
    return (void*) tracked_cf_release;
  }
  if (strcmp(symbol, "CFBundleGetFunctionPointerForName") == 0) {
    *(void**) &cf_bundle_get_function_pointer_for_name = result;
    return (void*) tracked_cf_bundle_get_function_pointer_for_name;
  }
  return result;
}


/* Track the helper's owned references, excluding framework-internal memory. */
#define dlsym tracked_dlsym
#define getenv tracked_getenv
#define uv__set_process_title test_darwin_set_process_title
#define uv__thread_setname uv_thread_setname
#include "../src/unix/darwin-proctitle.c"
#undef uv__thread_setname
#undef uv__set_process_title
#undef getenv
#undef dlsym


static void reset_process_title_darwin_test(int asn_available, char* opt_in) {
  cf_create_calls = 0;
  cf_string_count = 0;
  cf_release_count = 0;
  cf_fail_at = 0;
  ls_get_asn_count = 0;
  ls_checkin_count = 0;
  ls_set_info_count = 0;
  ls_call_order_count = 0;
  ls_checkin_tag = 0;
  ls_connection_status = 0;
  ls_connection = NULL;
  ls_set_info_tag = 0;
  ls_set_info_asn = NULL;
  ls_asn_available = asn_available;
  ls_asn_available_after_checkin = 1;
  ls_set_info_status = noErr;
  process_title_env_value = opt_in;
}
#endif


TEST_IMPL(process_title_darwin_launch_services) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  char thread_name[64];
  uv_thread_t thread;
  int err;

  reset_process_title_darwin_test(0, NULL);
  err = test_darwin_set_process_title("process title test");
  ASSERT_EQ(ls_checkin_count, 0);
  ASSERT_EQ(ls_set_info_count, 0);
  ASSERT_EQ(err, UV_EBUSY);
  ASSERT_EQ(ls_get_asn_count, 1);
  ASSERT_EQ(ls_call_order_count, 1);
  ASSERT_EQ(ls_call_order[0], ls_call_get_asn);
  ASSERT_EQ(cf_string_count, cf_release_count);
  thread = uv_thread_self();
  ASSERT_OK(uv_thread_getname(&thread, thread_name, sizeof(thread_name)));
  ASSERT_OK(strcmp(thread_name, "process title test"));

  reset_process_title_darwin_test(0, "0");
  err = test_darwin_set_process_title("process title test");
  ASSERT_EQ(ls_checkin_count, 0);
  ASSERT_EQ(ls_set_info_count, 0);
  ASSERT_EQ(err, UV_EBUSY);
  ASSERT_EQ(cf_string_count, cf_release_count);

  reset_process_title_darwin_test(1, NULL);
  err = test_darwin_set_process_title("process title test");
  ASSERT_OK(err);
  ASSERT_EQ(ls_checkin_count, 0);
  ASSERT_EQ(ls_set_info_count, 1);
  ASSERT_EQ(ls_call_order_count, 2);
  ASSERT_EQ(ls_call_order[0], ls_call_get_asn);
  ASSERT_EQ(ls_call_order[1], ls_call_set_info);
  ASSERT_EQ(ls_set_info_tag, -2);
  ASSERT_PTR_EQ(ls_set_info_asn, (CFTypeRef) &ls_test_asn);
  ASSERT_EQ(cf_string_count, cf_release_count);

  /* An ASN can exist before the process checks in with LaunchServices. */
  reset_process_title_darwin_test(1, "1");
  err = test_darwin_set_process_title("process title test");
  ASSERT_EQ(ls_checkin_count, 1);
  ASSERT_OK(err);
  ASSERT_EQ(ls_set_info_count, 1);
  ASSERT_EQ(ls_get_asn_count, 1);
  ASSERT_EQ(cf_string_count, cf_release_count);

  reset_process_title_darwin_test(1, NULL);
  ls_set_info_status = -600;
  err = test_darwin_set_process_title("process title fallback");
  ASSERT_EQ(err, UV_EINVAL);
  ASSERT_EQ(ls_checkin_count, 0);
  ASSERT_EQ(ls_set_info_count, 1);
  ASSERT_EQ(cf_string_count, cf_release_count);
  ASSERT_OK(uv_thread_getname(&thread, thread_name, sizeof(thread_name)));
  ASSERT_OK(strcmp(thread_name, "process title fallback"));

  reset_process_title_darwin_test(0, "1");
  err = test_darwin_set_process_title("process title test");
  ASSERT_OK(err);
  ASSERT_EQ(ls_checkin_count, 1);
  ASSERT_EQ(ls_set_info_count, 1);
  ASSERT_EQ(ls_call_order_count, 4);
  ASSERT_EQ(ls_call_order[0], ls_call_set_connection_status);
  ASSERT_EQ(ls_call_order[1], ls_call_check_in);
  ASSERT_EQ(ls_call_order[2], ls_call_get_asn);
  ASSERT_EQ(ls_call_order[3], ls_call_set_info);
  ASSERT_EQ(ls_checkin_tag, -2);
  ASSERT_EQ(ls_connection_status, 0);
  ASSERT_NULL(ls_connection);
  ASSERT_EQ(ls_set_info_tag, -2);
  ASSERT_PTR_EQ(ls_set_info_asn, (CFTypeRef) &ls_test_asn);
  ASSERT_EQ(cf_string_count, cf_release_count);

  reset_process_title_darwin_test(0, "1");
  ls_asn_available_after_checkin = 0;
  err = test_darwin_set_process_title("process title test");
  ASSERT_EQ(err, UV_EBUSY);
  ASSERT_EQ(ls_checkin_count, 1);
  ASSERT_EQ(ls_set_info_count, 0);
  ASSERT_EQ(ls_call_order_count, 3);
  ASSERT_EQ(ls_call_order[0], ls_call_set_connection_status);
  ASSERT_EQ(ls_call_order[1], ls_call_check_in);
  ASSERT_EQ(ls_call_order[2], ls_call_get_asn);
  ASSERT_EQ(cf_string_count, cf_release_count);

  return 0;
#else
  RETURN_SKIP("LaunchServices is only used for process titles on macOS.");
#endif
}


TEST_IMPL(process_title_cf_strings) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  unsigned int create_calls;
  unsigned int fail_at;
  unsigned int i;
  int err;

  create_calls = ARRAY_SIZE(cf_strings);
  for (fail_at = 0; fail_at <= create_calls; fail_at++) {
    reset_process_title_darwin_test(0, "1");
    cf_fail_at = fail_at;
    err = test_darwin_set_process_title("process title leak test");
    if (cf_fail_at != 0) {
      ASSERT_EQ(err, UV_ENOMEM);
      ASSERT_EQ(cf_create_calls, cf_fail_at);
    } else {
      /* LaunchServices can be unavailable or reject the display-name update. */
      ASSERT(err == 0 || err == UV_EINVAL ||
             err == UV_ENOENT || err == UV_EBUSY);
      create_calls = cf_create_calls;
    }
    ASSERT_EQ(cf_string_count, cf_release_count);
    for (i = 0; i < cf_string_count; i++)
      ASSERT_NULL(cf_strings[i]);
  }
  if (create_calls == 0)
    RETURN_SKIP("Core Foundation process-title functions are unavailable.");
  return 0;
#else
  RETURN_SKIP("Core Foundation is only used for process titles on macOS.");
#endif
}
