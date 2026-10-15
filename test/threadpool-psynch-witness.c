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

/* Manual Darwin oracle: controlled enrollment, real workers, no counter writes. */
#include "threadpool-darwin-wait.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifndef THREADPOOL_WITNESS_SOURCE
# define THREADPOOL_WITNESS_SOURCE "../src/threadpool.c"
#endif
#include THREADPOOL_WITNESS_SOURCE

static unsigned jobs;
static unsigned callbacks;

static double seconds(void) {
  struct timespec now;
  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  return now.tv_sec + now.tv_nsec / 1e9;
}

static void lock_parked(unsigned expected) {
  double deadline;
  uintptr_t condition;
  uint64_t generation;
  unsigned count;
  unsigned i;

  deadline = seconds() + 3;
  for (;;) {
    uv_mutex_lock(&mutex);
    count = 0;
    for (i = 0; i < expected; i++)
      count += read_wait_generation(threads[i], &condition, &generation) &&
               condition == (uintptr_t) &cond;
    if (idle_threads == expected && count == expected)
      return;
    uv_mutex_unlock(&mutex);
    assert(seconds() < deadline);
    sched_yield();
  }
}

static void work(uv_work_t* req) {
  (void) req;
  uv_mutex_lock(&mutex);
  jobs++;
  uv_mutex_unlock(&mutex);
}

static void after(uv_work_t* req, int status) {
  (void) req;
  assert(status == 0);
  callbacks++;
}

static void snapshot(const char* phase) {
  unsigned words[12];
  uintptr_t condition;
  uint64_t generation;
  unsigned i;

  memcpy(words, &cond, sizeof(words));
  printf("{\"phase\":\"%s\",\"idle\":%u,\"queueEmpty\":%d,"
         "\"L\":%u,\"S\":%u,\"U\":%u,\"jobs\":%u,"
         "\"callbacks\":%u,\"waitL\":[",
         phase, idle_threads, uv__queue_empty(&wq), words[6], words[7],
         words[8], jobs, callbacks);
  for (i = 0; i < nthreads; i++) {
    assert(read_wait_generation(threads[i], &condition, &generation));
    printf("%s%u", i ? "," : "", (unsigned) generation);
  }
  puts("]}");
}

int main(int argc, char** argv) {
  const struct timespec timeout = { 0, 1 };
  uv_thread_options_t options;
  uv_work_t requests[4];
  uv_loop_t loop;
  uv_sem_t sem;
  unsigned long age;
  unsigned long i;
  double started;
  double deadline;
  int result;

  assert(argc == 2);
  age = strtoul(argv[1], NULL, 10);
  assert(age == 1024 || age == 8388605);
  setbuf(stdout, NULL);
  alarm(600);
  assert(setenv("UV_THREADPOOL_SIZE", "1", 1) == 0);
  assert(uv_loop_init(&loop) == 0);
  uv_once(&once, init_once);
  assert(nthreads == 1 && threads == default_threads);
  lock_parked(1);

  /* Age one real parked worker using the same condition and the same mutex. */
  started = seconds();
  for (i = 0; i < age; i++) {
    result = pthread_cond_timedwait_relative_np(&cond, &mutex, &timeout);
    assert(result == ETIMEDOUT || result == 0);
  }
  printf("{\"aged\":%lu,\"seconds\":%.3f}\n", age, seconds() - started);

  /* Only enrollment is test-owned; queueing and execution use production code. */
  assert(uv_sem_init(&sem, 0) == 0);
  nthreads = 4;
  options.flags = UV_THREAD_HAS_STACK_SIZE;
  options.stack_size = 8u << 20;
  for (i = 1; i < 4; i++)
    assert(uv_thread_create_ex(threads + i, &options, worker, &sem) == 0);
  for (i = 1; i < 4; i++)
    uv_sem_wait(&sem);
  uv_sem_destroy(&sem);
  uv_mutex_unlock(&mutex);
  lock_parked(4);
  snapshot("before");
  uv_mutex_unlock(&mutex);

  for (i = 0; i < 4; i++)
    assert(uv_queue_work(&loop, requests + i, work, after) == 0);
  deadline = seconds() + 3;
  while (callbacks < 4 && seconds() < deadline) {
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_sleep(1);
  }
  lock_parked(4);
  snapshot("after");
  uv_mutex_unlock(&mutex);
  if (callbacks < 4)
    _Exit(10);
  assert(jobs == 4);
  assert(uv_loop_close(&loop) == 0);
  uv_library_shutdown();
  return 0;
}
