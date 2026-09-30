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
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  uv__handle_close(handle);
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
  /* Block new senders and wait for any in-flight send to finish. The wakeup
   * req is shared by the loop, so any IOCP notification still in flight does
   * not reference this handle and we can schedule the endgame immediately. */
  uv__async_spin(handle);
  uv__queue_remove(&handle->queue);
  uv__want_endgame(loop, (uv_handle_t*) handle);
  uv__handle_closing(handle);
}


void uv__async_notify(uv_async_t* handle) {
  uv_loop_t* loop = handle->loop;
  POST_COMPLETION_FOR_REQ(loop, &loop->async_req);
}


void uv__async_stop(uv_loop_t* loop) {
  struct uv__queue* q;
  uv_async_t* h;

  /* Spin all UV_ASYNC handles that are still open. */
  uv__queue_foreach(q, &loop->async_handles) {
    h = uv__queue_data(q, uv_async_t, queue);
    uv__async_spin(h);
  }

  /* Close the internal wq_async handle directly, bypassing the normal endgame:
   * any pending IOCP message will be discarded with loop->iocp. */
  uv__queue_remove(&loop->wq_async.queue);
  loop->wq_async.close_cb = NULL;
  uv__handle_closing(&loop->wq_async);
  uv__handle_close(&loop->wq_async);
}


void uv__process_async_wakeup_req(uv_loop_t* loop,
                                  uv_req_t* req) {
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;

  assert(req->type == UV_WAKEUP);

  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    /* Clear pending flag, retain busy counter. The InterlockedAnd is seq_cst
     * (a full barrier), and synchronizing with the seq_cst
     * InterlockedCompareExchange in uv_async_send. This makes all accesses
     * before that call visible here (and vice versa). */
    if (!(InterlockedAnd((LONG volatile*) &h->pending, ~1) & 1))
      continue;

    if (h->async_cb != NULL)
      h->async_cb(h);
  }
}
