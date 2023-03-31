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

#define RECURS_SIZE 1000
#define TASK_THREAD_SIZE 100

static int work_cb_count;
static int work_cb_count2;
static int after_work_cb_count;
static uv_work_t work_req;
static uv_sem_t work_sem;
static uv_mutex_t val_lock;
static uv_loop_t* main_loop;
static char data;


static void work_cb(uv_work_t* req) {
  ASSERT(req == &work_req);
  ASSERT(req->data == &data);
  work_cb_count++;
}


static void after_work_cb(uv_work_t* req, int status) {
  ASSERT(status == 0);
  ASSERT(req == &work_req);
  ASSERT(req->data == &data);
  after_work_cb_count++;
}


TEST_IMPL(threadpool_queue_work_simple) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, work_cb, after_work_cb);
  ASSERT(r == 0);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(work_cb_count == 1);
  ASSERT(after_work_cb_count == 1);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(threadpool_queue_work_einval) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, NULL, NULL);
  ASSERT(r == UV_EINVAL);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(work_cb_count == 0);
  ASSERT(after_work_cb_count == 0);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static uv_work_t* create_work_inst(void) {
  uv_work_t* req = malloc(sizeof(*req));
  ASSERT_NOT_NULL(req);
  return req;
}


/* This is necessary since uv_queue_work() requires the call to uv_after_work_cb
 * to be called, even in the case the req was cancelled. Only then is it removed
 * from the internal queue and able to be free'd by the user. */
static void after_work_recurs_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  free(req);
  after_work_cb_count++;
}


static void req_work(uv_loop_t* loop, uv_work_cb cb) {
  int r;
  uv_work_t* req2 = create_work_inst();
  r = uv_queue_work(loop, req2, cb, after_work_recurs_cb);
  ASSERT_OK(r);
}


static void work_recurs_cb(uv_work_t* req) {
  if (++work_cb_count >= RECURS_SIZE)
    return;
  req_work(req->loop, work_recurs_cb);
}


static void work_recurs2_cb(uv_work_t* req) {
  if (++work_cb_count2 >= RECURS_SIZE)
    return;
  req_work(req->loop, work_recurs2_cb);
}


TEST_IMPL(threadpool_queue_recursive) {
  uv_work_t* req1;
  uv_work_t* req2;

  req1 = create_work_inst();
  req2 = create_work_inst();

  ASSERT_OK(uv_queue_work(uv_default_loop(),
                          req1,
                          work_recurs_cb,
                          after_work_recurs_cb));
  ASSERT_OK(uv_queue_work(uv_default_loop(),
                          req2,
                          work_recurs2_cb,
                          after_work_recurs_cb));

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(work_cb_count, RECURS_SIZE);
  ASSERT_EQ(work_cb_count2, RECURS_SIZE);
  ASSERT_EQ(after_work_cb_count, RECURS_SIZE * 2);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void no_after_cb(uv_work_t* req) {
  if (++work_cb_count < RECURS_SIZE) {
    ASSERT_OK(uv_queue_work(NULL, req, no_after_cb, NULL));
    return;
  }
  uv_sem_post(&work_sem);
  free(req);
}


TEST_IMPL(threadpool_queue_no_after) {
  uv_work_t* req = malloc(sizeof(*req));

  ASSERT_OK(uv_sem_init(&work_sem, 0));
  ASSERT_EQ(uv_queue_work(uv_default_loop(), req, no_after_cb, NULL),
            UV_EINVAL);
  ASSERT_OK(uv_queue_work(NULL, req, no_after_cb, NULL));

  uv_sem_wait(&work_sem);

  ASSERT_EQ(work_cb_count, RECURS_SIZE);

  uv_sem_destroy(&work_sem);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void after_no_work_cb(uv_work_t* req, int status) {
  uv_thread_t self = uv_thread_self();

  ASSERT_OK(status);
  ASSERT(uv_thread_equal((uv_thread_t*)req->data, &self));
  work_cb_count++;
  free(req);
}


static void no_work_cb2(uv_work_t* req) {
  uv_thread_t self = uv_thread_self();

  ASSERT_OK(uv_queue_work(main_loop, req, NULL, after_no_work_cb));
  ASSERT_OK(uv_thread_equal((uv_thread_t*)req->data, &self));
  uv_sem_post(&work_sem);
}


TEST_IMPL(threadpool_queue_separately) {
  uv_work_t* req = malloc(sizeof(*req));
  uv_thread_t self = uv_thread_self();

  main_loop = uv_default_loop();
  req->data = &self;

  ASSERT_OK(uv_sem_init(&work_sem, 0));
  ASSERT_OK(uv_queue_work(NULL, req, no_work_cb2, NULL));

  uv_sem_wait(&work_sem);

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(work_cb_count, 1);
  uv_sem_destroy(&work_sem);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void after_from_thread_cb(uv_work_t* req, int status) {
  free(req);
  work_cb_count++;
}


static void from_thread_cb(uv_work_t* req) {
  free(req);
  uv_mutex_lock(&val_lock);
  work_cb_count2++;
  uv_mutex_unlock(&val_lock);
  uv_sem_post(&work_sem);
}


static void thread_cb(void* arg) {
  int i;
  for (i = 0; i < TASK_THREAD_SIZE; i++) {
    uv_work_t* req1 = malloc(sizeof(*req1));
    uv_work_t* req2 = malloc(sizeof(*req2));
    ASSERT_OK(uv_queue_work(NULL, req1, from_thread_cb, NULL));
    ASSERT_OK(uv_queue_work((uv_loop_t*)arg, req2, NULL, after_from_thread_cb));
  }
}


TEST_IMPL(threadpool_queue_from_thread) {
  uv_thread_t thread;
  int i;

  ASSERT_OK(uv_sem_init(&work_sem, 0));
  ASSERT_OK(uv_mutex_init(&val_lock));
  ASSERT_OK(uv_thread_create(&thread, thread_cb, uv_default_loop()));
  ASSERT_OK(uv_thread_join(&thread));

  for (i = 0; i < TASK_THREAD_SIZE; i++) {
    uv_sem_wait(&work_sem);
  }

  ASSERT_OK(work_cb_count);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(work_cb_count, TASK_THREAD_SIZE);
  ASSERT_EQ(work_cb_count2, TASK_THREAD_SIZE);
  uv_sem_destroy(&work_sem);
  uv_mutex_destroy(&val_lock);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void deadlock_work_cb(uv_work_t* req) {
  work_cb_count++;
  free(req);
  uv_sem_post(&work_sem);
}

/* Make sure queue'ing both a uv_work_cb and uv_after_work_cb doesn't cause a
 * deadlock with the internal mutex when queue'd from the after_work_cb. */
static void deadlock_after_cb(uv_work_t* req, int status) {
  uv_work_t* work_req;
  uv_work_t* after_work_req;

  after_work_cb_count++;
  if (after_work_cb_count == 1) {
    work_req = malloc(sizeof(*work_req));
    ASSERT_OK(uv_queue_work(NULL, work_req, deadlock_work_cb, NULL));
  }
  if (after_work_cb_count < 2) {
    after_work_req = malloc(sizeof(*after_work_req));
    ASSERT_OK(uv_queue_work(req->loop, after_work_req, NULL, deadlock_after_cb));
  }

  free(req);
}

static void deadlock_thread_cb(void* arg) {
  uv_work_t* req = malloc(sizeof(*req));

  ASSERT_OK(uv_queue_work((uv_loop_t*) arg, req, NULL, deadlock_after_cb));
}

TEST_IMPL(threadpool_try_deadlock) {
  uv_thread_t thread;
  uv_loop_t* loop;

  loop = uv_default_loop();

  ASSERT_OK(uv_sem_init(&work_sem, 0));
  ASSERT_OK(uv_thread_create(&thread, deadlock_thread_cb, loop));
  /* Join thread before uv_run() to make sure uv_queue_work() ran. */
  uv_thread_join(&thread);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(after_work_cb_count, 2);

  uv_sem_wait(&work_sem);
  ASSERT_EQ(work_cb_count, 1);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
