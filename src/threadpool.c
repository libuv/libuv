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

#include "uv-common.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#endif

#include <stdlib.h>

#define MAX_THREADPOOL_SIZE 128

static struct default_executor_fields {
  uv_cond_t cond;
  uv_mutex_t mutex;
  unsigned int idle_workers;
  unsigned int nworkers;
  uv_thread_t* workers;
  uv_thread_t default_workers[4];
  QUEUE exit_message;
  QUEUE wq;
} _fields;

static struct worker_arg {
  uv_executor_t* executor;
  uv_sem_t *ready;
} worker_arg;

/* executor */
static uv_executor_t default_executor;

/* Helpers for the default executor implementation. */

/* Post item q to the TP queue.
 * Caller must hold fields->lock. */
static void post(struct default_executor_fields* fields, QUEUE* q) {
  QUEUE_INSERT_TAIL(&fields->wq, q);
  if (0 < fields->idle_workers)
    uv_cond_signal(&fields->cond);
}

/* This is the entry point for each worker in the threadpool.
 * arg is a worker_arg*. */
static void worker(void* arg) {
  uv_executor_t* executor;
  struct uv__work* w;
  uv_work_t* req;
  QUEUE* q;
  struct default_executor_fields* fields;

  executor = ((struct worker_arg*) arg)->executor;
  fields = (struct default_executor_fields*) executor->data;

  uv_sem_post(((struct worker_arg*) arg)->ready);
  arg = NULL;

  uv_mutex_lock(&mutex);
  for (;;) {
    /* Get the next work. */
    uv_mutex_lock(&fields->mutex);

    while (QUEUE_EMPTY(&fields->wq)) {
      fields->idle_workers += 1;
      uv_cond_wait(&fields->cond, &fields->mutex);
      fields->idle_workers -= 1;
    }

    q = QUEUE_HEAD(&fields->wq);

    if (q == &fields->exit_message) {
      /* Wake up another thread. */
      uv_cond_signal(&fields->cond);
    }
    else {
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is
                             executing. */
    }

    uv_mutex_unlock(&fields->mutex);

    /* Are we done? */
    if (q == &fields->exit_message)
      break;

    w = QUEUE_DATA(q, struct uv__work, wq);
    req = container_of(w, uv_work_t, work_req);

    /* Do the work. */
    req->work_cb(req);

    /* Tell libuv we finished with this request. */
    executor->done(req);
  }
}

/* Initialize (fields members and) the workers. */
static void init_workers(struct default_executor_fields* fields) {
  unsigned int i;
  const char* val;
  uv_sem_t sem;
  unsigned int n_default_workers;

  /* Initialize various fields members. */

  /* How many workers? */
  n_default_workers = ARRAY_SIZE(fields->default_workers);
  fields->nworkers = n_default_workers;
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    fields->nworkers = atoi(val);
  if (fields->nworkers == 0)
    fields->nworkers = 1;
  if (fields->nworkers > MAX_THREADPOOL_SIZE)
    fields->nworkers = MAX_THREADPOOL_SIZE;

  /* Try to use the statically declared workers instead of malloc. */
  fields->workers = fields->default_workers;
  if (fields->nworkers > n_default_workers) {
    fields->workers = uv__malloc(fields->nworkers * sizeof(fields->workers[0]));
    if (fields->workers == NULL) {
      fields->nworkers = n_default_workers;
      fields->workers = fields->default_workers;
    }
  }

  if (uv_cond_init(&fields->cond))
    abort();

  if (uv_mutex_init(&fields->mutex))
    abort();

  QUEUE_INIT(&fields->wq);

  if (uv_sem_init(&sem, 0))
    abort();

  /* Start the workers. */
  worker_arg.executor = &default_executor;
  worker_arg.ready = &sem;
  for (i = 0; i < fields->nworkers; i++)
    if (uv_thread_create(fields->workers + i, worker, &worker_arg))
      abort();

  /* Wait for workers to start. */
  for (i = 0; i < fields->nworkers; i++)
    uv_sem_wait(&sem);
  uv_sem_destroy(&sem);
}

#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(struct default_executor_fields* fields)) {
  unsigned int i;

  if (fields->nworkers == 0)
    return;

  post(fields, &fields->exit_message);

  for (i = 0; i < fields->nworkers; i++)
    if (uv_thread_join(fields->workers + i))
      abort();

  if (fields->workers != fields->default_workers)
    uv__free(fields->workers);

  uv_mutex_destroy(&fields->mutex);
  uv_cond_destroy(&fields->cond);

  fields->workers = NULL;
  fields->nworkers = 0;

  /* fields itself is static, nothing to do. */
  assert(fields == &_fields);
}
#endif

/******************************
 * Default libuv threadpool, implemented using the executor API.
*******************************/

static void uv__default_executor_init(uv_executor_t* executor) {
  struct default_executor_fields* fields;

  assert(executor == &default_executor);

  fields = (struct default_executor_fields *) executor->data;

  /* TODO Behavior on fork. */

  init_workers(fields);
}

static void uv__default_executor_destroy(uv_executor_t* executor) {
  struct default_executor_fields* fields;

  fields = (struct default_executor_fields *) executor->data;
  cleanup(fields);
}

static void uv__default_executor_submit(uv_executor_t* executor,
                                        uv_work_t* req,
                                        const uv_work_options_t* opts) {
  struct default_executor_fields* fields;
  struct uv__work* wreq;

  fields = (struct default_executor_fields *) executor->data;

  /* Put executor-specific data into req->reserved[0]. */
  wreq = &req->work_req;
  req->reserved[0] = wreq;

  uv_mutex_lock(&fields->mutex);

  /* Add to our queue. */
  post(fields, &wreq->wq);

  uv_mutex_unlock(&fields->mutex);
}

static int uv__default_executor_cancel(uv_executor_t* executor, uv_work_t* req) {
  struct default_executor_fields* fields;
  struct uv__work* wreq;
  int cancelled;

  fields = (struct default_executor_fields *) executor->data;
  wreq = (struct uv__work *) req->reserved[0];

  uv_mutex_lock(&fields->mutex);

  /* Cancellable if it is still on the TP queue. */
  cancelled = !QUEUE_EMPTY(&wreq->wq);
  if (cancelled)
    QUEUE_REMOVE(&wreq->wq);

  uv_mutex_unlock(&fields->mutex);

  if (cancelled) {
    /* We are now done with req. Notify libuv. */
    executor->done(req);
    return 0;
  }
  else {
    /* Failed to cancel.
     * Work is either already done or is still to be executed.
     * Either way we need not call done here. */
    return UV_EBUSY;
  }
}

uv_executor_t * uv__default_executor(void) {
  /* This is only called once, internally by uv__executor_init.
   * So it's safe to set these fields here. */
  default_executor.data = &_fields;
  default_executor.init = uv__default_executor_init;
  default_executor.destroy = uv__default_executor_destroy;
  default_executor.submit = uv__default_executor_submit;
  default_executor.cancel = uv__default_executor_cancel;

  return &default_executor;
}
