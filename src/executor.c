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

#include <stdlib.h>  /* abort() */

static uv_executor_t *executor = NULL;

static int safe_to_replace_executor = 1;
static uv_once_t once = UV_ONCE_INIT;

uv_executor_t * uv__executor(void) {
  return executor;
}

/* CB: Executor has finished this request. */
static void uv__executor_done(uv_work_t* req) {
  uv_mutex_lock(&req->loop->wq_mutex);

  /* Place in the associated loop's queue.
   * NB We are re-purposing req->work_req.wq here.
   * This field is also used by the default executor, but
   * not after the default executor has called done() to get here. */
  QUEUE_INSERT_TAIL(&req->loop->wq, &req->work_req.wq);

  /* Signal to loop that there's a pending task. */
  uv_async_send(&req->loop->wq_async);

  uv_mutex_unlock(&req->loop->wq_mutex);
}

/* CB: Event loop is handling completed requests. */
void uv__executor_work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_work_t* req;
  uv_loop_t* loop;
  QUEUE* q;
  QUEUE wq;
  int err;

  /* Grab this loop's completed work. */
  loop = container_of(handle, uv_loop_t, wq_async);
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_MOVE(&loop->wq, &wq);
  uv_mutex_unlock(&loop->wq_mutex);

  /* Pull uv__work's off of wq.
   * Run their uv_work_t's after_work_cb, if any. */
  while (!QUEUE_EMPTY(&wq)) {
    q = QUEUE_HEAD(&wq);
    QUEUE_REMOVE(q);

    w = container_of(q, struct uv__work, wq);
    req = container_of(w, uv_work_t, work_req);
    err = (req->work_cb == uv__executor_work_cancelled) ? UV_ECANCELED : 0;

    uv__req_unregister(req->loop, req);

    if (req->after_work_cb != NULL)
      req->after_work_cb(req, err);
  }
}

int uv_executor_replace(uv_executor_t* _executor) {
  /* Reject if no longer safe to replace. */
  if (!safe_to_replace_executor)
    return 1;

  /* Check validity of _executor. */
  if (_executor == NULL)
    return 1;
  if (_executor->submit == NULL)
    return 1;

  /* Set private fields. */
  _executor->done = uv__executor_done;

  /* Replace our executor. */
  executor = _executor;

  return 0;
}

static void uv__executor_init(void) {
  /* Assign executor to default if none was set. */
  if (executor == NULL) {
    executor = uv__default_executor();
  }

  executor->init(executor);
}

int uv_executor_queue_work(uv_loop_t* loop,
                           uv_work_t* req,
                           uv_work_options_t* opts,
                           uv_work_cb work_cb,
                           uv_after_work_cb after_work_cb) {
  /* Once work is queued, it is no longer safe to replace the executor. */
  safe_to_replace_executor = 0;

  /* Initialize the executor once. */
  uv_once(&once, uv__executor_init);

  /* Check validity. */
  if (loop == NULL || req == NULL || work_cb == NULL)
    return UV_EINVAL;

  /* Register req on loop. */
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

  /* Submit to the executor. */
  executor->submit(executor, req, opts);

  return 0;
}

int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  /* Deprecated. */
  return uv_executor_queue_work(loop, req, NULL, work_cb, after_work_cb);
}

int uv_cancel(uv_req_t* req) {
  uv_work_t* work;
  int r;

  r = UV_EINVAL;
  switch (req->type) {
  /* Currently, only requests submitted to an executor can be cancelled. */
  case UV_FS:
  case UV_GETADDRINFO:
  case UV_GETNAMEINFO:
  case UV_WORK:
    if (executor->cancel != NULL) {
      work = (uv_work_t *) req;
      r = executor->cancel(executor, work);
      if (r == 0) {
        work->work_cb = uv__executor_work_cancelled;
      }
    }
  default:
    return UV_EINVAL;
  }

  return r;
}

/* This is just a magic, it should never be called. */
void uv__executor_work_cancelled(uv_work_t* work) {
  abort();
}
