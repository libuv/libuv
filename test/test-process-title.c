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
static CFStringRef cf_strings[7];
static unsigned int cf_create_calls;
static unsigned int cf_string_count;
static unsigned int cf_release_count;
static unsigned int cf_fail_at;
static unsigned int dlopen_calls;
static int block_dlopen;


static void* tracked_dlopen(const char* path, int mode) {
  dlopen_calls++;
  if (block_dlopen)
    return NULL;
  return dlopen(path, mode);
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
  return result;
}


/* Track framework loads and references, excluding framework-internal memory. */
#define dlopen tracked_dlopen
#define dlsym tracked_dlsym
#define uv__process_title_init test_darwin_process_title_init
#define uv__set_process_title test_darwin_set_process_title
#define uv__thread_setname uv_thread_setname
#include "../src/unix/darwin-proctitle.c"
#undef uv__thread_setname
#undef uv__set_process_title
#undef uv__process_title_init
#undef dlsym
#undef dlopen
#endif


TEST_IMPL(process_title_launch_services) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  const char* values[] = { NULL, "", "0", "1", "00", "false" };
  const char* titles[] = { "first title", "second title" };
  uv_thread_t self;
  char name[64];
  unsigned int i;
  unsigned int j;
  int enabled;
  int err;

  self = uv_thread_self();
  block_dlopen = 1;
  for (i = 0; i < ARRAY_SIZE(values); i++) {
    if (values[i] == NULL)
      ASSERT_OK(uv_os_unsetenv("UV_PROCESS_TITLE_USE_LAUNCH_SERVICES"));
    else
      ASSERT_OK(uv_os_setenv("UV_PROCESS_TITLE_USE_LAUNCH_SERVICES",
                            values[i]));
    enabled = values[i] == NULL || strcmp(values[i], "0") != 0;
    test_darwin_process_title_init();

    /* Changes after initialization must not affect subsequent title updates. */
    ASSERT_OK(uv_os_setenv("UV_PROCESS_TITLE_USE_LAUNCH_SERVICES",
                          enabled ? "0" : "1"));
    for (j = 0; j < ARRAY_SIZE(titles); j++) {
      dlopen_calls = 0;
      err = test_darwin_set_process_title(titles[j]);
      if (enabled) {
        ASSERT_EQ(err, UV_ENOENT);
        ASSERT_EQ(dlopen_calls, 2);
      } else {
        ASSERT_OK(err);
        ASSERT_EQ(dlopen_calls, 0);
        ASSERT_OK(uv_thread_getname(&self, name, sizeof(name)));
        ASSERT_STR_EQ(name, titles[j]);
      }
    }
  }
  return 0;
#else
  RETURN_SKIP("LaunchServices is only used for process titles on macOS.");
#endif
}


TEST_IMPL(process_title_cf_strings) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  unsigned int create_calls;
  unsigned int i;
  int err;

  create_calls = ARRAY_SIZE(cf_strings);
  for (cf_fail_at = 0; cf_fail_at <= create_calls; cf_fail_at++) {
    cf_create_calls = 0;
    cf_string_count = 0;
    cf_release_count = 0;
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


#ifdef _WIN32
static void* shutdown_ptrs[8];
static unsigned int shutdown_nptrs;


static void shutdown_track(void* ptr) {
  if (ptr == NULL)
    return;
  ASSERT_LT(shutdown_nptrs, ARRAY_SIZE(shutdown_ptrs));
  shutdown_ptrs[shutdown_nptrs++] = ptr;
}


static void shutdown_untrack(void* ptr) {
  unsigned int i;

  for (i = 0; i < shutdown_nptrs; i++) {
    if (shutdown_ptrs[i] == ptr) {
      shutdown_ptrs[i] = shutdown_ptrs[--shutdown_nptrs];
      return;
    }
  }
}


static void* shutdown_malloc(size_t size) {
  void* ptr;

  ptr = malloc(size);
  shutdown_track(ptr);
  return ptr;
}


static void* shutdown_realloc(void* ptr, size_t size) {
  void* new_ptr;

  new_ptr = realloc(ptr, size);
  if (new_ptr != NULL) {
    shutdown_untrack(ptr);
    shutdown_track(new_ptr);
  }
  return new_ptr;
}


static void* shutdown_calloc(size_t count, size_t size) {
  void* ptr;

  ptr = calloc(count, size);
  shutdown_track(ptr);
  return ptr;
}


static void shutdown_free(void* ptr) {
  shutdown_untrack(ptr);
  free(ptr);
}
#endif


TEST_IMPL(process_title_shutdown) {
#ifdef _WIN32
  char buffer[512];

  /* Initialize libuv and cache the default title before swapping allocators,
   * so the only live tracked allocation is the title set below.
   */
  ASSERT_OK(uv_get_process_title(buffer, sizeof(buffer)));
  ASSERT_OK(uv_replace_allocator(shutdown_malloc,
                                 shutdown_realloc,
                                 shutdown_calloc,
                                 shutdown_free));

  ASSERT_OK(uv_set_process_title("libuv shutdown test"));
  ASSERT_OK(uv_get_process_title(buffer, sizeof(buffer)));
  ASSERT_OK(strcmp(buffer, "libuv shutdown test"));
  ASSERT_EQ(1, shutdown_nptrs);

  uv_library_shutdown();
  ASSERT_EQ(0, shutdown_nptrs);
  return 0;
#else
  RETURN_SKIP("Windows only test");
#endif
}
