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

#include <assert.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "req-inl.h"


void uv__async_endgame(uv_loop_t* loop, uv_async_t* handle) {
<<<<<<< HEAD
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  uv__handle_close(handle);
||||||| 1cfa32ff5
  if (handle->flags & UV_HANDLE_CLOSING &&
      !handle->async_sent) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    uv__handle_close(handle);
  }
=======
  /* uv__async_close guarantees uv__want_endgame is called exactly once: the
   * spin drains all in-flight senders and the return value selects which path
   * schedules the endgame, so no double-close guard is needed here. */
  if (handle->flags & UV_HANDLE_CLOSING) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    uv__handle_close(handle);
  }
>>>>>>> v1.53.0
}


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_ASYNC);
  handle->pending = 0;
  handle->async_cb = async_cb;

  uv__queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


void uv__async_close(uv_loop_t* loop, uv_async_t* handle) {
<<<<<<< HEAD
  uv__queue_remove(&handle->queue);
  uv__want_endgame(loop, (uv_handle_t*) handle);
||||||| 1cfa32ff5
  if (!((uv_async_t*)handle)->async_sent) {
    uv__want_endgame(loop, (uv_handle_t*) handle);
  }

=======
  /* Block new senders and wait for any in-flight send to finish.
   * uv__async_spin returns the previous value of the pending flag (bit 0):
   * if it was already set, an IOCP notification is in flight and will trigger
   * the endgame via uv__process_async_wakeup_req; otherwise we schedule the
   * endgame immediately because no further IOCP completion will arrive. */
  if (!uv__async_spin(handle))
    uv__want_endgame(loop, (uv_handle_t*) handle);

>>>>>>> v1.53.0
  uv__handle_closing(handle);
}


<<<<<<< HEAD
int uv_async_send(uv_async_t* handle) {
  /* First do a cheap read. */
  if (handle->async_sent != 0)
    return 0;
||||||| 1cfa32ff5
int uv_async_send(uv_async_t* handle) {
  uv_loop_t* loop = handle->loop;
=======
void uv__async_notify(uv_async_t* handle) {
  uv_loop_t* loop = handle->loop;
  POST_COMPLETION_FOR_REQ(loop, &handle->async_req);
}
>>>>>>> v1.53.0

<<<<<<< HEAD
  if (InterlockedExchange(&handle->async_sent, 1) == 0) {
    uv_loop_t* loop = handle->loop;
    POST_COMPLETION_FOR_REQ(loop, &loop->async_req);
  }

  return 0;
||||||| 1cfa32ff5
  if (handle->type != UV_ASYNC) {
    /* Can't set errno because that's not thread-safe. */
    return -1;
  }

  /* The user should make sure never to call uv_async_send to a closing or
   * closed handle. */
  assert(!(handle->flags & UV_HANDLE_CLOSING));

  if (!uv__atomic_exchange_set(&handle->async_sent)) {
    POST_COMPLETION_FOR_REQ(loop, &handle->async_req);
  }

  return 0;
=======

void uv__async_stop(uv_loop_t* loop) {
  struct uv__queue* q;
  uv_handle_t* h;

  /* Spin all UV_ASYNC handles that are still open. */
  uv__queue_foreach(q, &loop->handle_queue) {
    h = uv__queue_data(q, uv_handle_t, handle_queue);
    if (h->type == UV_ASYNC)
      uv__async_spin((uv_async_t*) h);
  }

  /* Close the internal wq_async handle directly, bypassing the normal endgame:
   * any pending IOCP message will be discarded with loop->iocp. */
  loop->wq_async.close_cb = NULL;
  uv__handle_closing(&loop->wq_async);
  uv__handle_close(&loop->wq_async);
>>>>>>> v1.53.0
}


void uv__process_async_wakeup_req(uv_loop_t* loop,
                                  uv_req_t* req) {
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;

  assert(req->type == UV_WAKEUP);

<<<<<<< HEAD
  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);
||||||| 1cfa32ff5
  handle->async_sent = 0;
=======
  /* Clear pending flag, retain busy counter. The InterlockedAnd is seq_cst (a
   * full barrier), and synchronizing with the seq_cst
   * InterlockedCompareExchange in uv_async_send. This makes all accesses
   * before that call visible here (and vice versa). */
  InterlockedAnd((LONG volatile*) &handle->pending, ~1);
>>>>>>> v1.53.0

<<<<<<< HEAD
    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    if (InterlockedExchange(&h->async_sent, 0) == 0)
      continue;

    if (h->async_cb != NULL)
      h->async_cb(h);
||||||| 1cfa32ff5
  if (handle->flags & UV_HANDLE_CLOSING) {
    uv__want_endgame(loop, (uv_handle_t*)handle);
  } else if (handle->async_cb != NULL) {
    handle->async_cb(handle);
=======
  if (handle->flags & UV_HANDLE_CLOSING) {
    uv__want_endgame(loop, (uv_handle_t*) handle);
  } else if (handle->async_cb != NULL) {
    handle->async_cb(handle);
>>>>>>> v1.53.0
  }
}
