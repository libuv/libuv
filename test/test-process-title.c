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
#if !TARGET_OS_IPHONE
#include <CoreFoundation/CoreFoundation.h>
#endif
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
static struct {
  uv_mutex_t mutex;
  size_t allocations;
  size_t outstanding;
} cf_allocator;


static void* cf_allocate(CFIndex size, CFOptionFlags flags, void* info) {
  void* ptr;

  ptr = malloc(size);
  if (ptr != NULL) {
    uv_mutex_lock(&cf_allocator.mutex);
    cf_allocator.allocations++;
    cf_allocator.outstanding++;
    uv_mutex_unlock(&cf_allocator.mutex);
  }
  return ptr;
}


static void cf_deallocate(void* ptr, void* info) {
  if (ptr == NULL)
    return;

  uv_mutex_lock(&cf_allocator.mutex);
  ASSERT_GT(cf_allocator.outstanding, 0);
  cf_allocator.outstanding--;
  uv_mutex_unlock(&cf_allocator.mutex);
  free(ptr);
}


static void* cf_reallocate(void* ptr,
                           CFIndex size,
                           CFOptionFlags flags,
                           void* info) {
  if (ptr == NULL)
    return cf_allocate(size, flags, info);
  if (size == 0) {
    cf_deallocate(ptr, info);
    return NULL;
  }
  return realloc(ptr, size);
}
#endif


TEST_IMPL(process_title_no_leak) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  CFAllocatorRef (*pCFAllocatorCreate)(CFAllocatorRef, CFAllocatorContext*);
  CFAllocatorRef (*pCFAllocatorGetDefault)(void);
  void (*pCFAllocatorSetDefault)(CFAllocatorRef);
  void (*pCFRelease)(CFTypeRef);
  CFAllocatorContext context;
  CFAllocatorRef previous_allocator;
  CFAllocatorRef allocator;
  size_t allocations;
  size_t outstanding;
  uv_lib_t library;
  int i;

  ASSERT_OK(uv_dlopen("/System/Library/Frameworks/CoreFoundation.framework/"
                     "Versions/A/CoreFoundation", &library));
  ASSERT_OK(uv_dlsym(&library, "CFAllocatorCreate",
                    (void**) &pCFAllocatorCreate));
  ASSERT_OK(uv_dlsym(&library, "CFAllocatorGetDefault",
                    (void**) &pCFAllocatorGetDefault));
  ASSERT_OK(uv_dlsym(&library, "CFAllocatorSetDefault",
                    (void**) &pCFAllocatorSetDefault));
  ASSERT_OK(uv_dlsym(&library, "CFRelease", (void**) &pCFRelease));

  /* Core Foundation keeps a default allocator alive even after replacement. */
  ASSERT_OK(uv_mutex_init(&cf_allocator.mutex));
  memset(&context, 0, sizeof(context));
  context.allocate = cf_allocate;
  context.reallocate = cf_reallocate;
  context.deallocate = cf_deallocate;
  allocator = pCFAllocatorCreate(NULL, &context);
  ASSERT_NOT_NULL(allocator);
  previous_allocator = pCFAllocatorGetDefault();
  pCFAllocatorSetDefault(allocator);

  /* Warm up framework caches before measuring repeated identical calls. */
  for (i = 0; i < 10; i++)
    set_title("process title leak test");
  uv_mutex_lock(&cf_allocator.mutex);
  allocations = cf_allocator.allocations;
  outstanding = cf_allocator.outstanding;
  uv_mutex_unlock(&cf_allocator.mutex);

  for (i = 0; i < 10; i++)
    set_title("process title leak test");
  pCFAllocatorSetDefault(previous_allocator);
  pCFRelease(allocator);

  uv_mutex_lock(&cf_allocator.mutex);
  ASSERT_GT(cf_allocator.allocations, allocations);
  ASSERT_EQ(cf_allocator.outstanding, outstanding);
  uv_mutex_unlock(&cf_allocator.mutex);
  uv_dlclose(&library);
  return 0;
#else
  RETURN_SKIP("Core Foundation is only used for process titles on macOS.");
#endif
}
