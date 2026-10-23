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

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__x86_64__))
# include "threadpool-darwin-wait.h"

static uv_work_t requests[4];
static uv_thread_t workers[4];
static uv_sem_t enrolled;
static uv_sem_t release_workers;
static unsigned completed;

static void enroll_worker(uv_work_t* req) {
  workers[req - requests] = uv_thread_self();
  uv_sem_post(&enrolled);
  uv_sem_wait(&release_workers);
}

static void work_cb(uv_work_t* req) {
  (void) req;
}

static void complete_work(uv_work_t* req, int status) {
  (void) req;
  ASSERT_OK(status);
  completed++;
}

static int snapshot_waits(uint64_t generations[4]) {
  uint64_t deadline;
  unsigned i;
  int parked;
  uintptr_t condition;

  deadline = uv_hrtime() + 3000000000ULL;
  do {
    parked = 1;
    for (i = 0; i < ARRAY_SIZE(workers); i++)
      if (!read_wait_generation(workers[i], &condition, generations + i))
        parked = 0;
    if (parked)
      return 1;
    uv_sleep(1);
  } while (uv_hrtime() < deadline);
  return 0;
}
#endif

TEST_IMPL(threadpool_idle_wait_refresh) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__x86_64__))
  uint64_t before[4];
  uint64_t after[4];
  uv_work_t final_request;
  unsigned i;

  ASSERT_OK(uv_os_setenv("UV_THREADPOOL_SIZE", "4"));
  ASSERT_OK(uv_sem_init(&enrolled, 0));
  ASSERT_OK(uv_sem_init(&release_workers, 0));
  for (i = 0; i < ARRAY_SIZE(requests); i++)
    ASSERT_OK(uv_queue_work(uv_default_loop(), requests + i,
                           enroll_worker, complete_work));
  for (i = 0; i < ARRAY_SIZE(requests); i++)
    uv_sem_wait(&enrolled);
  for (i = 0; i < ARRAY_SIZE(requests); i++)
    uv_sem_post(&release_workers);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(4, completed);
  uv_sem_destroy(&enrolled);
  uv_sem_destroy(&release_workers);

  if (!snapshot_waits(before)) {
    MAKE_VALGRIND_HAPPY(uv_default_loop());
    RETURN_SKIP("Darwin psynch waits are not active");
  }
  /* With no submitted work, every idle waiter must leave its old generation. */
  uv_sleep(31000);
  ASSERT(snapshot_waits(after));
  for (i = 0; i < ARRAY_SIZE(workers); i++) {
    ASSERT_NE((uint32_t) before[i], (uint32_t) after[i]);
    ASSERT_LE((uint32_t) after[i] - (uint32_t) before[i], 16 * 256U);
  }

  ASSERT_OK(uv_queue_work(uv_default_loop(), &final_request,
                         work_cb, complete_work));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(5, completed);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#else
  RETURN_SKIP("Darwin psynch-specific test");
#endif
}
