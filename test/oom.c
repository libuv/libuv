/* Copyright libuv contributors. All rights reserved.
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

/* Allocator for testing out-of-memory handling. It counts live allocations
 * across all threads, but only fails allocations on the thread that last
 * called oom_fail() so that other threads (e.g. the threadpool or the
 * FSEvents thread) are unaffected.
 */

#include "uv.h"
#include "task.h"
#include <stdlib.h>

static uv_mutex_t oom_mutex;
static uv_key_t oom_key;
static int oom_live_count;
static int oom_skip;
static int oom_count;
static int oom_failed;


static int oom_should_fail(void) {
  /* Only the thread that armed the injection state reads or writes it. */
  if (uv_key_get(&oom_key) == NULL)
    return 0;
  if (oom_skip > 0) {
    oom_skip--;
    return 0;
  }
  if (oom_count == 0)
    return 0;
  if (oom_count > 0)
    oom_count--;
  oom_failed++;
  return 1;
}


static void oom_track(void* ptr, int delta) {
  if (ptr == NULL)
    return;
  uv_mutex_lock(&oom_mutex);
  oom_live_count += delta;
  uv_mutex_unlock(&oom_mutex);
}


static void* oom_malloc(size_t size) {
  void* ptr;

  if (oom_should_fail())
    return NULL;
  ptr = malloc(size);
  oom_track(ptr, 1);
  return ptr;
}


static void* oom_calloc(size_t count, size_t size) {
  void* ptr;

  if (oom_should_fail())
    return NULL;
  ptr = calloc(count, size);
  oom_track(ptr, 1);
  return ptr;
}


static void* oom_realloc(void* ptr, size_t size) {
  void* newptr;

  if (oom_should_fail())
    return NULL;
  newptr = realloc(ptr, size);
  if (ptr == NULL)
    oom_track(newptr, 1);
  return newptr;
}


static void oom_free(void* ptr) {
  oom_track(ptr, -1);
  free(ptr);
}


void oom_init(void) {
  ASSERT_OK(uv_mutex_init(&oom_mutex));
  ASSERT_OK(uv_key_create(&oom_key));
  oom_live_count = 0;
  oom_skip = 0;
  oom_count = 0;
  oom_failed = 0;
  ASSERT_OK(uv_replace_allocator(oom_malloc,
                                 oom_realloc,
                                 oom_calloc,
                                 oom_free));
}


void oom_fail(int skip, int count) {
  uv_key_set(&oom_key, &oom_key);
  oom_skip = skip;
  oom_count = count;
  oom_failed = 0;
}


int oom_failures(void) {
  return oom_failed;
}


int oom_live(void) {
  int live;

  uv_mutex_lock(&oom_mutex);
  live = oom_live_count;
  uv_mutex_unlock(&oom_mutex);
  return live;
}


void oom_cleanup(void) {
  /* Memory allocated before oom_init() may be freed with the default free. */
  ASSERT_OK(uv_replace_allocator(malloc, realloc, calloc, free));
  uv_key_delete(&oom_key);
  uv_mutex_destroy(&oom_mutex);
}
