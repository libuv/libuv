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

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memset */

#ifdef __POSIX__
#include <pthread.h>
#include <errno.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

uv_sem_t sem;

static void simple_task(void *args) {
    uv_sem_wait(&sem);
    printf("in simple_task\n");
}

TEST_IMPL(thread_priority) {
  int priority;
  int r;
#ifndef _WIN32
  int min;
  int max;
#endif
  uv_thread_t task_id;

  /* Verify that passing a NULL pointer returns UV_EINVAL. */
  ASSERT_EQ(UV_EINVAL, uv_thread_getpriority(0, NULL));

  r = uv_sem_init(&sem, 1);
  ASSERT(r == 0);

  uv_sem_wait(&sem);

  r = uv_thread_create(&task_id, simple_task, NULL);
  ASSERT(r == 0);

  r = uv_thread_getpriority(task_id, &priority);
  ASSERT(r == 0);
#ifdef _WIN32
  ASSERT(priority == THREAD_PRIORITY_NORMAL);
#else
  int policy;
  struct sched_param param;
  r = pthread_getschedparam(task_id, &policy, &param);
  ASSERT(r == 0);
#ifdef __PASE__
  min = 1;
  max = 127;
#else
  min = sched_get_priority_min(policy);
  max = sched_get_priority_max(policy);
#endif
  ASSERT(priority >= min && priority <= max);
#endif

  r = uv_thread_setpriority(task_id, UV_THREAD_PRIORITY_LOWEST);
  ASSERT(r == 0);
  r = uv_thread_getpriority(task_id, &priority);
  ASSERT(r == 0);
#ifdef _WIN32
  ASSERT(priority == THREAD_PRIORITY_LOWEST);
#else
  ASSERT(priority == min);
#endif

/**
 * test set nice value for the calling thread with default schedule policy
*/
#ifdef __linux__
  r = uv_thread_getpriority(pthread_self(), &priority);
  ASSERT(r == 0);
  ASSERT(priority == 0);
  r = uv_thread_setpriority(pthread_self(), UV_THREAD_PRIORITY_LOWEST);
  r = uv_thread_getpriority(pthread_self(), &priority);
  ASSERT(r == 0);
  ASSERT(priority == (0 - UV_THREAD_PRIORITY_LOWEST * 2));
#endif

  uv_sem_post(&sem);

  r = uv_thread_join(&task_id);
  ASSERT_OK(r);

  uv_sem_destroy(&sem);

  return 0;
}