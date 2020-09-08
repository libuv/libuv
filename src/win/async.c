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


void uv_async_endgame(uv_loop_t* loop, uv_async_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  uv__handle_close(handle);
}


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_ASYNC);
  handle->async_sent = 0;
  handle->async_cb = async_cb;

  QUEUE_INSERT_TAIL(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


void uv_async_close(uv_loop_t* loop, uv_async_t* handle) {
  QUEUE_REMOVE(&handle->queue);
  uv_want_endgame(loop, (uv_handle_t*) handle);
  uv__handle_closing(handle);
}


int uv_async_send(uv_async_t* handle) {
  /* First do a cheap read. */
  if (handle->async_sent != 0)
    return 0;

  if (InterlockedExchange(&handle->async_sent, 1) == 0) {
    uv_loop_t* loop = handle->loop;
    POST_COMPLETION_FOR_REQ(loop, &loop->async_req);
  }

  return 0;
}


void uv_process_async_wakeup_req(uv_loop_t* loop,
                                 uv_req_t* req) {
  QUEUE queue;
  QUEUE* q;
  uv_async_t* h;

  assert(req->type == UV_WAKEUP);

  QUEUE_MOVE(&loop->async_handles, &queue);
  while (!QUEUE_EMPTY(&queue)) {
    q = QUEUE_HEAD(&queue);
    h = QUEUE_DATA(q, uv_async_t, queue);

    QUEUE_REMOVE(q);
    QUEUE_INSERT_TAIL(&loop->async_handles, q);

    if (InterlockedExchange(&h->async_sent, 0) == 0)
      continue;

    if (h->async_cb != NULL)
      h->async_cb(h);
  }
}
