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
#define IDLE_THREAD_TIMEOUT 5e9  /* 5 seconds in nanoseconds. */

enum { EMPTY, ACTIVE, DEAD };

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int busy_threads;
static unsigned int num_threads;
static unsigned int max_threads;
static char threads_bitmap[MAX_THREADPOOL_SIZE];
static uv_thread_t threads[MAX_THREADPOOL_SIZE];
static QUEUE exit_message;
static QUEUE wq;
static volatile int initialized;


static void uv__cancelled(struct uv__work* w) {
  abort();
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  uintptr_t thread_idx;
  struct uv__work* w;
  QUEUE* q;

  thread_idx = (uintptr_t) arg;

  for (;;) {
    uv_mutex_lock(&mutex);

    while (QUEUE_EMPTY(&wq)) {
      /* If we are the first thread, just wait, don't terminate; it might not
       * be possible to spin up a new thread again due to resource restrictions
       * like RLIMIT_NPROC.
       */
      if (thread_idx == 0) {
        uv_cond_wait(&cond, &mutex);
        continue;
      }

      if (uv_cond_timedwait(&cond, &mutex, IDLE_THREAD_TIMEOUT)) {
        threads_bitmap[thread_idx] = DEAD;
        num_threads -= 1;
        uv_mutex_unlock(&mutex);
        return;
      }
    }

    q = QUEUE_HEAD(&wq);

    if (q == &exit_message) {
      threads_bitmap[thread_idx] = DEAD;
      num_threads -= 1;
      uv_cond_signal(&cond);
    } else {
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is executing. */
      busy_threads += 1;
    }

    uv_mutex_unlock(&mutex);

    if (q == &exit_message)
      return;

    w = QUEUE_DATA(q, struct uv__work, wq);
    w->work(w);

    uv_mutex_lock(&w->loop->wq_mutex);
    busy_threads -= 1;
    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);
  }
}


/* Only call when you hold the global mutex.  maybe_resize_threadpool() can
 * drop the lock when executing but it will be locked again on return.
 */
static void maybe_resize_threadpool(void) {
  uintptr_t thread_idx;
  int join_thread;
  int err;

  if (busy_threads < num_threads)
    return;

  if (max_threads == num_threads)
    return;

  /* Find the first available slot in the threads array.  Skip checking
   * the first slot in the normal case because the first thread doesn't
   * terminate when it's idle like the other threads do, it's always active.
   */
  thread_idx = (num_threads > 0);
  for (; threads_bitmap[thread_idx] == ACTIVE; thread_idx += 1) {
    /* Can't happen because max_threads <= ARRAY_SIZE(threads_bitmap). */
    assert(thread_idx < ARRAY_SIZE(threads_bitmap));
  }

  /* Do we need to reap the thread before we can reuse the slot? */
  join_thread = (threads_bitmap[thread_idx] == DEAD);

  /* Mark the thread as active now so another thread won't also try to join
   * it when we drop the lock.  And we need to drop the lock because thread
   * cleanup can be slow.
   */
  threads_bitmap[thread_idx] = ACTIVE;
  num_threads += 1;

  /* Drop the lock for a second, joining and creating threads can be slow. */
  uv_mutex_unlock(&mutex);

  if (join_thread)
    if (uv_thread_join(threads + thread_idx))
      abort();

  err = uv_thread_create(threads + thread_idx, worker, (void*) thread_idx);
  uv_mutex_lock(&mutex);

  if (err == 0)
    return;

  threads_bitmap[thread_idx] = EMPTY;
  num_threads -= 1;

  if (num_threads == 0)
    abort();  /* No forward progress, couldn't start the first thread. */
}


/* Only call when you hold the global mutex.  post_unlocked() can
 * drop the lock when executing but it will be locked again on return.
 */
static void post_unlocked(QUEUE* q) {
  maybe_resize_threadpool();
  QUEUE_INSERT_TAIL(&wq, q);
  uv_cond_signal(&cond);
}


static void post(QUEUE* q) {
  uv_mutex_lock(&mutex);
  post_unlocked(q);
  uv_mutex_unlock(&mutex);
}


#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  unsigned int i;

  if (initialized == 0)
    return;

  uv_mutex_lock(&mutex);

  /* Tell threads to terminate. */
  if (num_threads > 0)
    post_unlocked(&exit_message);

  /* Wait for threads to terminate. */
  while (num_threads > 0) {
    uv_cond_wait(&cond, &mutex);
    uv_cond_signal(&cond);
  }

  for (i = 0; i < ARRAY_SIZE(threads); i += 1) {
    assert(threads_bitmap[i] != ACTIVE);

    if (threads_bitmap[i] != DEAD)
      continue;

    if (uv_thread_join(threads + i))
      abort();

    threads_bitmap[i] = EMPTY;
  }

  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  initialized = 0;
}
#endif


static void init_once(void) {
  const char* val;

  max_threads = 8;  /* FIXME(bnoordhuis) Arbitrary default. */
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    max_threads = atoi(val);
  if (max_threads == 0)
    max_threads = 1;
  if (max_threads > ARRAY_SIZE(threads))
    max_threads = ARRAY_SIZE(threads);

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  QUEUE_INIT(&wq);

  initialized = 1;
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq);
}


static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

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


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
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
