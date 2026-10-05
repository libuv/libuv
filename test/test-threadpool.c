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

#ifdef _WIN32
# define putenv _putenv
#endif

static int work_cb_count;
static int after_work_cb_count;
static uv_work_t work_req;
static char data;


static void work_cb(uv_work_t* req) {
  ASSERT_PTR_EQ(req, &work_req);
  ASSERT_PTR_EQ(req->data, &data);
  work_cb_count++;
}


static void after_work_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_PTR_EQ(req, &work_req);
  ASSERT_PTR_EQ(req->data, &data);
  after_work_cb_count++;
}


TEST_IMPL(threadpool_queue_work_simple) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, work_cb, after_work_cb);
  ASSERT_OK(r);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, work_cb_count);
  ASSERT_EQ(1, after_work_cb_count);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(threadpool_queue_work_einval) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, NULL, after_work_cb);
  ASSERT_EQ(r, UV_EINVAL);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_OK(work_cb_count);
  ASSERT_OK(after_work_cb_count);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static uv_work_t blocking_reqs[4];
static uv_sem_t blocking_sems[ARRAY_SIZE(blocking_reqs)];
static uv_sem_t blocking_started_sem;
static int stat_cb_count;


static void blocking_work_cb(uv_work_t* req) {
  uv_sem_post(&blocking_started_sem);
  uv_sem_wait(blocking_sems + (req - blocking_reqs));
}


static void blocking_after_work_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  uv_sem_destroy(blocking_sems + (req - blocking_reqs));
  after_work_cb_count++;
}


static void stat_cb(uv_fs_t* req) {
  ASSERT_OK(req->result);
  /* The CPU work is still blocked. */
  ASSERT_OK(after_work_cb_count);
  uv_fs_req_cleanup(req);
  stat_cb_count++;
}


TEST_IMPL(threadpool_cpu_work_does_not_starve_io) {
  char env[] = "UV_THREADPOOL_SIZE=4";
  uv_loop_t* loop;
  uv_fs_t req;
  size_t i;

  ASSERT_OK(putenv(env));
  loop = uv_default_loop();

  ASSERT_OK(uv_sem_init(&blocking_started_sem, 0));
  for (i = 0; i < ARRAY_SIZE(blocking_reqs); i++) {
    ASSERT_OK(uv_sem_init(blocking_sems + i, 0));
    ASSERT_OK(uv_queue_work(loop,
                            blocking_reqs + i,
                            blocking_work_cb,
                            blocking_after_work_cb));
  }

  /* UV_THREADPOOL_SIZE units of CPU work run concurrently. */
  for (i = 0; i < ARRAY_SIZE(blocking_reqs); i++)
    uv_sem_wait(&blocking_started_sem);
  uv_sem_destroy(&blocking_started_sem);

  /* The thread reserved for I/O still serves file system requests. */
  ASSERT_OK(uv_fs_stat(loop, &req, ".", stat_cb));
  while (stat_cb_count == 0)
    uv_run(loop, UV_RUN_ONCE);

  for (i = 0; i < ARRAY_SIZE(blocking_reqs); i++)
    uv_sem_post(blocking_sems + i);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, stat_cb_count);
  ASSERT_EQ(4, after_work_cb_count);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
