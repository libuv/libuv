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

#define MAX_THREADPOOL_SIZE 1024

/* How long a worker that just completed a request keeps polling for the next
 * one before it parks itself, and how that is rationed; see worker(). */
#ifndef UV__THREADPOOL_LINGER_NS
#define UV__THREADPOOL_LINGER_NS (50 * 1000)
#endif
#define LINGER_NS UV__THREADPOOL_LINGER_NS
#define LINGER_CREDIT_MAX 8
#define LINGER_PROBE_INTERVAL 64

#ifdef _MSC_VER
#define uv__seq_load(p) ((unsigned int) InterlockedOr((LONG volatile*) (p), 0))
#define uv__seq_bump(p) ((void) InterlockedIncrement((LONG volatile*) (p)))
typedef LONG uv__seq_t;
#else
#define uv__seq_load(p) atomic_load_explicit((p), memory_order_relaxed)
#define uv__seq_bump(p)                                                       \
  ((void) atomic_fetch_add_explicit((p), 1, memory_order_relaxed))
typedef _Atomic unsigned int uv__seq_t;
#endif

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_threads;
static unsigned int linger_enabled; /* Never on a single CPU: spinning there
                                       only delays the thread we wait for. */
static unsigned int lingering;      /* A worker polls `wq`, see worker(). */
static unsigned int linger_credit;  /* Protected by `mutex`, like the above. */
static unsigned int linger_probe;
static uv__seq_t post_seq;          /* Bumped on every insertion into `wq`. */
static unsigned int slow_io_work_running;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[4];
static struct uv__queue exit_message;
static struct uv__queue wq;
static struct uv__queue run_slow_work_message;
static struct uv__queue slow_io_pending_wq;

static unsigned int slow_work_thread_threshold(void) {
  return (nthreads + 1) / 2;
}

static void uv__cancelled(struct uv__work* w) {
  abort();
}


/* Decide whether a worker that found the queue empty right after completing
 * a request should poll for the next one instead of sleeping. Submitters very
 * often post their next request within microseconds of the previous one
 * completing - sequential file system calls are the obvious example - and
 * catching it this way saves the submitter a wake-up system call and saves
 * the request the latency of unparking a thread.
 *
 * Only one worker lingers at a time. Lingering is rationed with a simple
 * credit scheme so that workloads where it does not pay off (requests spaced
 * further apart than LINGER_NS) stop doing it: a linger that catches work
 * refills the credit, one that times out consumes one, and once the credit is
 * gone only every LINGER_PROBE_INTERVAL-th opportunity is used to probe
 * whether the pattern has changed. Worst case that is one wasted LINGER_NS
 * per LINGER_PROBE_INTERVAL completed requests. Called with `mutex` held.
 */
static int uv__worker_may_linger(void) {
  if (!linger_enabled || lingering != 0 || !uv__queue_empty(&wq))
    return 0;

  if (linger_credit > 0)
    return 1;

  if (++linger_probe < LINGER_PROBE_INTERVAL)
    return 0;

  linger_probe = 0;
  return 1;
}


/* Poll `post_seq` until it changes or LINGER_NS have passed. Called with
 * `mutex` unlocked; the caller re-checks the queue under the lock either way.
 * Returns nonzero if something was posted.
 */
static int uv__worker_linger(unsigned int seq) {
  uint64_t deadline;
  int i;

  deadline = uv_hrtime() + LINGER_NS;
  do {
    for (i = 0; i < 64; i++) {
      if (uv__seq_load(&post_seq) != seq)
        return 1;
      uv__cpu_relax();
    }
  } while (uv_hrtime() < deadline);

  return uv__seq_load(&post_seq) != seq;
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  struct uv__queue* q;
  unsigned int seq;
  int is_slow_work;
  int just_completed;
  int caught;

  uv_thread_setname("libuv-worker");
  uv_sem_post((uv_sem_t*) arg);
  arg = NULL;
  just_completed = 0;

  uv_mutex_lock(&mutex);
  for (;;) {
    /* `mutex` should always be locked at this point. */

    /* Keep waiting while either no work is present or only slow I/O
       and we're at the threshold for that. */
    while (uv__queue_empty(&wq) ||
           (uv__queue_head(&wq) == &run_slow_work_message &&
            uv__queue_next(&run_slow_work_message) == &wq &&
            slow_io_work_running >= slow_work_thread_threshold())) {
      if (just_completed && uv__worker_may_linger()) {
        just_completed = 0;
        lingering = 1;
        seq = uv__seq_load(&post_seq);
        uv_mutex_unlock(&mutex);
        caught = uv__worker_linger(seq);
        uv_mutex_lock(&mutex);
        lingering = 0;
        if (caught)
          linger_credit = LINGER_CREDIT_MAX;
        else if (linger_credit > 0)
          linger_credit--;
        continue;  /* Re-check the queue. */
      }
      just_completed = 0;
      idle_threads += 1;
      uv_cond_wait(&cond, &mutex);
      idle_threads -= 1;
    }
    just_completed = 1;

    q = uv__queue_head(&wq);
    if (q == &exit_message) {
      uv_cond_signal(&cond);
      uv_mutex_unlock(&mutex);
      break;
    }

    uv__queue_remove(q);
    uv__queue_init(q);  /* Signal uv_cancel() that the work req is executing. */

    is_slow_work = 0;
    if (q == &run_slow_work_message) {
      /* If we're at the slow I/O threshold, re-schedule until after all
         other work in the queue is done. */
      if (slow_io_work_running >= slow_work_thread_threshold()) {
        uv__queue_insert_tail(&wq, q);
        continue;
      }

      /* If we encountered a request to run slow I/O work but there is none
         to run, that means it's cancelled => Start over. */
      if (uv__queue_empty(&slow_io_pending_wq))
        continue;

      is_slow_work = 1;
      slow_io_work_running++;

      q = uv__queue_head(&slow_io_pending_wq);
      uv__queue_remove(q);
      uv__queue_init(q);

      /* If there is more slow I/O work, schedule it to be run as well. */
      if (!uv__queue_empty(&slow_io_pending_wq)) {
        uv__queue_insert_tail(&wq, &run_slow_work_message);
        if (idle_threads > 0)
          uv_cond_signal(&cond);
      }
    }

    uv_mutex_unlock(&mutex);

    w = uv__queue_data(q, struct uv__work, wq);
    w->work(w);

    uv_mutex_lock(&w->loop->wq_mutex);
    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    uv__queue_insert_tail(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);

    /* Lock `mutex` since that is expected at the start of the next
     * iteration. */
    uv_mutex_lock(&mutex);
    if (is_slow_work) {
      /* `slow_io_work_running` is protected by `mutex`. */
      slow_io_work_running--;
    }
  }
}


static void post(struct uv__queue* q, enum uv__work_kind kind) {
  int was_empty;

  uv_mutex_lock(&mutex);
  was_empty = uv__queue_empty(&wq);
  if (kind == UV__WORK_SLOW_IO) {
    /* Insert into a separate queue. */
    uv__queue_insert_tail(&slow_io_pending_wq, q);
    if (!uv__queue_empty(&run_slow_work_message)) {
      /* Running slow I/O tasks is already scheduled => Nothing to do here.
         The worker that runs said other task will schedule this one as well. */
      uv_mutex_unlock(&mutex);
      return;
    }
    q = &run_slow_work_message;
  }

  uv__queue_insert_tail(&wq, q);
  uv__seq_bump(&post_seq);
  /* A lingering worker always re-checks the queue under `mutex` before it
   * goes to sleep, so if one is active and this is the only item queued it is
   * guaranteed to be picked up; waking another thread would be redundant. */
  if (idle_threads > 0 && !(lingering && was_empty))
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifdef __MVS__
/* TODO(itodorov) - zos: revisit when Woz compiler is available. */
__attribute__((destructor))
#endif
void uv__threadpool_cleanup(void) {
  unsigned int i;

  if (nthreads == 0)
    return;

#ifndef __MVS__
  /* TODO(gabylb) - zos: revisit when Woz compiler is available. */
  post(&exit_message, UV__WORK_CPU);
#endif

  for (i = 0; i < nthreads; i++)
    if (uv_thread_join(threads + i))
      abort();

  if (threads != default_threads)
    uv__free(threads);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  threads = NULL;
  nthreads = 0;
}


static void init_threads(void) {
  uv_thread_options_t config;
  unsigned int i;
  size_t buflen;
  char buf[16];
  const char* val;
  int err;

  uv_sem_t sem;

  nthreads = ARRAY_SIZE(default_threads);

  buflen = ARRAY_SIZE(buf);
  err = uv_os_getenv("UV_THREADPOOL_SIZE", buf, &buflen);
  val = NULL;
  if (err == 0)
    val = buf;
  
  if (val != NULL)
    nthreads = atoi(val);
  if (nthreads == 0)
    nthreads = 1;
  if (nthreads > MAX_THREADPOOL_SIZE)
    nthreads = MAX_THREADPOOL_SIZE;

  threads = default_threads;
  if (nthreads > ARRAY_SIZE(default_threads)) {
    threads = uv__malloc(nthreads * sizeof(threads[0]));
    if (threads == NULL) {
      nthreads = ARRAY_SIZE(default_threads);
      threads = default_threads;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  /* May be stale in a forked child; the counters above are merely hints. */
  linger_enabled = uv_available_parallelism() >= 2;
  lingering = 0;
  linger_credit = LINGER_CREDIT_MAX;
  linger_probe = 0;
  uv__queue_init(&wq);
  uv__queue_init(&slow_io_pending_wq);
  uv__queue_init(&run_slow_work_message);

  if (uv_sem_init(&sem, 0))
    abort();

  config.flags = UV_THREAD_HAS_STACK_SIZE;
  config.stack_size = 8u << 20;  /* 8 MB */

  for (i = 0; i < nthreads; i++)
    if (uv_thread_create_ex(threads + i, &config, worker, &sem))
      abort();

  for (i = 0; i < nthreads; i++)
    uv_sem_wait(&sem);

  uv_sem_destroy(&sem);
}


#ifndef _WIN32
static void reset_once(void) {
  uv_once_t child_once = UV_ONCE_INIT;
  memcpy(&once, &child_once, sizeof(child_once));
}
#endif


static void init_once(void) {
#ifndef _WIN32
  /* Re-initialize the threadpool after fork.
   * Note that this discards the global mutex and condition as well
   * as the work queue.
   */
  if (pthread_atfork(NULL, NULL, &reset_once))
    abort();
#endif
  init_threads();
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq, kind);
}


/* TODO(bnoordhuis) teach libuv how to cancel file operations
 * that go through io_uring instead of the thread pool.
 */
static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_once(&once, init_once);  /* Ensure |mutex| is initialized. */
  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !uv__queue_empty(&w->wq) && w->work != NULL;
  if (cancelled)
    uv__queue_remove(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  uv__queue_insert_tail(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  struct uv__queue* q;
  struct uv__queue wq;
  int err;
  int nevents;

  loop = container_of(handle, uv_loop_t, wq_async);
  uv_mutex_lock(&loop->wq_mutex);
  uv__queue_move(&loop->wq, &wq);
  uv_mutex_unlock(&loop->wq_mutex);

  nevents = 0;

  while (!uv__queue_empty(&wq)) {
    q = uv__queue_head(&wq);
    uv__queue_remove(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    w->done(w, err);
    nevents++;
  }

  /* This check accomplishes 2 things:
   * 1. Even if the queue was empty, the call to uv__work_done() should count
   *    as an event. Which will have been added by the event loop when
   *    calling this callback.
   * 2. Prevents accidental wrap around in case nevents == 0 events == 0.
   */
  if (nevents > 1) {
    /* Subtract 1 to counter the call to uv__work_done(). */
    uv__metrics_inc_events(loop, nevents - 1);
    if (uv__get_internal_fields(loop)->current_timeout == 0)
      uv__metrics_inc_events_waiting(loop, nevents - 1);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop);

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
  uv__work_submit(loop,
                  &req->work_req,
                  UV__WORK_CPU,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_WRITE:
    if (uv__is_closing(((uv_write_t*) req)->handle))
      return UV_EBUSY;
    return uv__write_cancel((uv_write_t*) req);
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
  case UV_RANDOM:
    loop = ((uv_random_t*) req)->loop;
    wreq = &((uv_random_t*) req)->work_req;
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
