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
#include "task.h"
#include "../src/uv-common.h"

#include <string.h>

struct semaphores {
  uv_sem_t main;
  uv_sem_t worker;
};

static void thread_run(void* arg) {
  int r;
  char thread_name[16];
  struct semaphores* sem;
  uv_thread_t thread;

  sem = arg;

#ifdef _WIN32
  /* uv_thread_self isn't defined for the main thread on Windows. */
  thread = GetCurrentThread();
#else
  thread = uv_thread_self();
#endif

  r = uv_thread_setname("worker-thread");
  ASSERT_OK(r);

  uv_sem_post(&sem->worker);

  r = uv_thread_getname(&thread, thread_name, sizeof(thread_name));
  ASSERT_OK(r);

  ASSERT_STR_EQ(thread_name, "worker-thread");

  uv_sem_wait(&sem->main);
}

TEST_IMPL(thread_name) {
  int r;
  uv_thread_t threads[2];
  char tn[UV_PTHREAD_MAX_NAMELEN_NP];
  char thread_name[UV_PTHREAD_MAX_NAMELEN_NP];
  char long_thread_name[UV_PTHREAD_MAX_NAMELEN_NP + 1];
  struct semaphores sem;

#if defined(__MINGW32__) || \
    defined(__ANDROID_API__) && __ANDROID_API__ < 26 || \
    defined(_AIX) || \
    defined(__MVS__) || \
    defined(__PASE__)
  RETURN_SKIP("API not available on this platform");
#endif

  ASSERT_OK(uv_sem_init(&sem.main, 0));
  ASSERT_OK(uv_sem_init(&sem.worker, 0));

  memset(thread_name, 'a', sizeof(thread_name) - 1);
  thread_name[sizeof(thread_name) - 1] = '\0';

  memset(long_thread_name, 'a', sizeof(long_thread_name) - 1);
  long_thread_name[sizeof(long_thread_name) - 1] = '\0';

#ifdef _WIN32
  /* uv_thread_self isn't defined for the main thread on Windows. */
  threads[0] = GetCurrentThread();
#else
  threads[0] = uv_thread_self();
#endif

  r = uv_thread_getname(&threads[0], tn, sizeof(tn));
  ASSERT_OK(r);

  r = uv_thread_setname(long_thread_name);
  ASSERT_OK(r);

  r = uv_thread_getname(&threads[0], tn, sizeof(tn));
  ASSERT_OK(r);
  ASSERT_STR_EQ(tn, thread_name);

  r = uv_thread_setname(thread_name);
  ASSERT_OK(r);

  r = uv_thread_getname(&threads[0], tn, sizeof(tn));
  ASSERT_OK(r);
  ASSERT_STR_EQ(tn, thread_name);

  r = uv_thread_getname(&threads[0], tn, 3);
  ASSERT_OK(r);
  ASSERT_EQ(strlen(tn), 2);
  ASSERT_OK(memcmp(thread_name, tn, 2));

  /* Illumos doesn't support non-ASCII thread names. */
#ifndef __illumos__
  r = uv_thread_setname("~½¬{½");
  ASSERT_OK(r);

  r = uv_thread_getname(&threads[0], tn, sizeof(tn));
  ASSERT_OK(r);
  ASSERT_STR_EQ(tn, "~½¬{½");
#endif

  ASSERT_OK(uv_thread_create(threads + 1, thread_run, &sem));

  uv_sem_wait(&sem.worker);

  r = uv_thread_getname(threads + 1, tn, sizeof(tn));
  ASSERT_OK(r);

  ASSERT_STR_EQ(tn, "worker-thread");

  uv_sem_post(&sem.main);

  ASSERT_OK(uv_thread_join(threads + 1));

  uv_sem_destroy(&sem.main);
  uv_sem_destroy(&sem.worker);

  return 0;
}

