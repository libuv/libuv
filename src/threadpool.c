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

#include <limits.h>
#include <stdlib.h>

#define MAX_THREADPOOL_SIZE 128
#define IDLE_THREAD_TIMEOUT 5e9  /* 5 seconds in nanoseconds. */

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static int busy_threads;
static int num_threads;
static int max_threads;
static QUEUE exit_message;
static QUEUE wq;
static volatile int initialized;


static void uv__cancelled(struct uv__work* w) {
  abort();
}


/* Only call when you hold NO locks. */
static void do_work(struct uv__work* w) {
  w->work(w);
  uv_mutex_lock(&w->loop->wq_mutex);
  w->work = NULL;  /* Signal uv_cancel() that the work req is done executing. */
  QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
  uv_async_send(&w->loop->wq_async);
  uv_mutex_unlock(&w->loop->wq_mutex);
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
#ifdef _WIN32
static unsigned __stdcall worker(void* arg) {
#else
static void* worker(void* arg) {
#endif
  QUEUE* q;

  (void) &arg;
  uv_mutex_lock(&mutex);

  for (;;) {
    if (QUEUE_EMPTY(&wq))
      uv_cond_timedwait(&cond, &mutex, IDLE_THREAD_TIMEOUT);

    if (QUEUE_EMPTY(&wq))
      break;  /* Timed out, nothing to do, exit thread. */

    q = QUEUE_HEAD(&wq);

    if (q == &exit_message) {
      uv_cond_signal(&cond);  /* Propagate exit message to next thread. */
      break;
    }

    QUEUE_REMOVE(q);
    QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is executing. */

    busy_threads += 1;
    uv_mutex_unlock(&mutex);
    do_work(QUEUE_DATA(q, struct uv__work, wq));
    uv_mutex_lock(&mutex);
    busy_threads -= 1;
  }

  assert(num_threads > INT_MIN);  /* Extremely paranoid sanity check. */
  num_threads -= 1;
  uv_mutex_unlock(&mutex);

  return 0;
}


static int new_detached_thread(void) {
#ifdef _WIN32
  HANDLE thread;

  thread = (HANDLE) _beginthreadex(NULL, 0, worker, NULL, 0, NULL);
  if (thread == NULL)
    return -errno;

  if (CloseHandle(thread))
    abort();

  return 0;
#else
  pthread_t thread;
  int err;

  err = pthread_create(&thread, NULL, worker, NULL);
  if (err)
    return -err;

  if (pthread_detach(thread))
    abort();

  return 0;
#endif
}


#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  if (initialized == 0)
    return;

  uv_mutex_lock(&mutex);

  /* Tell threads to terminate. */
  if (num_threads > 0) {
    QUEUE_INSERT_TAIL(&wq, &exit_message);
    uv_cond_signal(&cond);
  }

  /* Wait for threads to terminate. */
  while (num_threads > 0) {
    uv_cond_wait(&cond, &mutex);
    uv_cond_signal(&cond);
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
  if (max_threads < 1)
    max_threads = 1;
  if (max_threads > MAX_THREADPOOL_SIZE)
    max_threads = MAX_THREADPOOL_SIZE;
  /* Allow forcing synchronous mode for testing purposes. */
  if (val != NULL && strcmp(val, "really-zero") == 0)
    max_threads = 0;

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
  int err;

  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;

  uv_mutex_lock(&mutex);

  if (busy_threads >= num_threads && num_threads < max_threads) {
    /* Drop the lock for a moment, starting a thread can be slow. */
    uv_mutex_unlock(&mutex);
    err = new_detached_thread();
    uv_mutex_lock(&mutex);

    /* There is technically a race window between new_detached_thread() where
     * the new thread exits and decrements num_threads before we increment it
     * here.  It's a benign race, though; worst case, num_threads is less than
     * zero for a short time.  We just take that in stride, the important part
     * is that num_threads > 0 means there is a thread to service the work item.
     */
    if (err == 0)
      num_threads += 1;
  }

  if (num_threads > 0) {
    QUEUE_INSERT_TAIL(&wq, &w->wq);
    uv_cond_signal(&cond);
    uv_mutex_unlock(&mutex);
  } else {
    /* If we can't start a thread, fall back to doing the work synchronously.
     * Performance will be degraded but at least we make forward progress.
     */
    QUEUE_INIT(&w->wq);
    uv_mutex_unlock(&mutex);
    do_work(w);
  }
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
