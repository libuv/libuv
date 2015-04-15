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
#else
# include "win/req-inl.h"
/* TODO(saghul): unify internal req functions */
static void uv__req_init(uv_loop_t* loop,
                         uv_req_t* req,
                         uv_req_type type) {
  uv_req_init(loop, req);
  req->type = type;
  uv__req_register(loop, req);
}
# define uv__req_init(loop, req, type) \
    uv__req_init((loop), (uv_req_t*)(req), (type))
#endif

#include <stdlib.h>

#define MAX_THREADPOOL_SIZE 128

struct uv_threadpool_s {
  uv_once_t once;
  uv_cond_t cond;
  uv_mutex_t mutex;
  unsigned int nthreads;
  uv_thread_t* threads;
  uv_thread_t default_threads[4];
  QUEUE exit_message;
  QUEUE wq;
  volatile int initialized;
};

static uv_threadpool_t default_pool = { .once = UV_ONCE_INIT };

static void uv__cancelled(struct uv__work* w) {
  abort();
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  QUEUE* q;

  uv_threadpool_t *tp = arg;

  for (;;) {
    uv_mutex_lock(&tp->mutex);

    while (QUEUE_EMPTY(&tp->wq))
      uv_cond_wait(&tp->cond, &tp->mutex);

    q = QUEUE_HEAD(&tp->wq);

    if (q == &tp->exit_message)
      uv_cond_signal(&tp->cond);
    else {
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is
                             executing. */
    }

    uv_mutex_unlock(&tp->mutex);

    if (q == &tp->exit_message)
      break;

    w = QUEUE_DATA(q, struct uv__work, wq);
    w->work(w);

    uv_mutex_lock(&w->loop->wq_mutex);
    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);
  }
}


static void post(uv_threadpool_t *tp, QUEUE* q) {
  uv_mutex_lock(&tp->mutex);
  QUEUE_INSERT_TAIL(&tp->wq, q);
  uv_cond_signal(&tp->cond);
  uv_mutex_unlock(&tp->mutex);
}


static void uv_threadpool_destroy(uv_threadpool_t *tp) {
  unsigned int i;

  if (tp->initialized == 0)
    return;

  post(tp, &tp->exit_message);

  for (i = 0; i < tp->nthreads; i++)
    if (uv_thread_join(tp->threads + i))
      abort();

  if (tp->threads != tp->default_threads)
    uv__free(tp->threads);

  uv_mutex_destroy(&tp->mutex);
  uv_cond_destroy(&tp->cond);

  tp->threads = NULL;
  tp->nthreads = 0;
  tp->initialized = 0;
}

#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  uv_threadpool_destroy(&default_pool);
}
#endif


void uv_tp_init_once(uv_threadpool_t *tp) {
  unsigned int i;
  const char* val;

  tp->nthreads = ARRAY_SIZE(tp->default_threads);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    tp->nthreads = atoi(val);
  if (tp->nthreads == 0)
    tp->nthreads = 1;
  if (tp->nthreads > MAX_THREADPOOL_SIZE)
    tp->nthreads = MAX_THREADPOOL_SIZE;

  tp->threads = tp->default_threads;
  if (tp->nthreads > ARRAY_SIZE(tp->default_threads)) {
    tp->threads = uv__malloc(tp->nthreads * sizeof(tp->threads[0]));
    if (tp->threads == NULL) {
      tp->nthreads = ARRAY_SIZE(tp->default_threads);
      tp->threads = tp->default_threads;
    }
  }

  if (uv_cond_init(&tp->cond))
    abort();

  if (uv_mutex_init(&tp->mutex))
    abort();

  QUEUE_INIT(&tp->wq);

  for (i = 0; i < tp->nthreads; i++)
    if (uv_thread_create(tp->threads + i, worker, NULL))
      abort();

  tp->initialized = 1;
}

static void init_once(void) {
  uv_tp_init_once(&default_pool);
}


void uv__work_tp_submit(uv_loop_t* loop,
                     uv_threadpool_t *tp,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  if (tp == NULL || tp == &default_pool) {
    tp = &default_pool;
    uv_once(&tp->once, init_once);
  }
  w->loop = loop;
  w->threadpool = tp;
  w->work = work;
  w->done = done;
  post(tp, &w->wq);
}

void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv__work_tp_submit(loop, NULL, w, work, done);
}


static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_threadpool_t *tp = w->threadpool;
  uv_mutex_lock(&tp->mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&tp->mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  QUEUE* q;
  QUEUE wq;
  int err;

  loop = container_of(handle, uv_loop_t, wq_async);
  QUEUE_INIT(&wq);

  uv_mutex_lock(&loop->wq_mutex);
  if (!QUEUE_EMPTY(&loop->wq)) {
    q = QUEUE_HEAD(&loop->wq);
    QUEUE_SPLIT(&loop->wq, q, &wq);
  }
  uv_mutex_unlock(&loop->wq_mutex);

  while (!QUEUE_EMPTY(&wq)) {
    q = QUEUE_HEAD(&wq);
    QUEUE_REMOVE(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    w->done(w, err);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

  req->after_work_cb(req, err);
}


int uv_queue_tp_work(uv_loop_t* loop,
                  uv_threadpool_t *tp,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_tp_submit(loop, tp, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
}

int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  return uv_queue_tp_work(loop, &default_pool, req, work_cb, after_work_cb);
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}
