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
#include <string.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"


int uv_overlapped_init(uv_loop_t* loop, uv_overlapped_t* handle) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_OVERLAPPED);
  handle->overlapped_cb = NULL;

  UV_REQ_INIT(&handle->overlapped_req, UV_OVERLAPPED_REQ);
  handle->overlapped_req.data = handle;

  OVERLAPPED *overlapped = &handle->overlapped_req.u.io.overlapped;
  memset(overlapped, 0, sizeof(*overlapped));

  return 0;
}


HANDLE uv_overlapped_get_iocp(uv_overlapped_t* handle) {
  return handle->loop->iocp;
}


OVERLAPPED* uv_overlapped_get_overlapped(uv_overlapped_t* handle) {
  return &handle->overlapped_req.u.io.overlapped;
}


void uv_overlapped_start(uv_overlapped_t* handle,
                         uv_overlapped_cb overlapped_cb) {
  assert(!(handle->flags & UV__HANDLE_CLOSING));
  assert(!uv__is_active(handle));
  assert(handle->overlapped_cb == NULL);
  assert(overlapped_cb != NULL);

  uv__handle_start(handle);
  handle->overlapped_cb = overlapped_cb;
}


void uv_overlapped_close(uv_loop_t* loop, uv_overlapped_t* handle) {
  /* Check uv__is_active before uv__handle_closing since that makes */
  /* it not active. */
  int active = uv__is_active(handle);
  assert(active == (handle->overlapped_cb != NULL));

  uv__handle_closing(handle);

  if (active) {
    /* we will wait for completion (uv_process_overlapped_req) */
  } else {
    if (handle->close_cb == NULL) {
      /* close immediately */
      uv_overlapped_endgame(loop, handle);
    } else {
      /* queue handle for calling the close callback */
      uv_want_endgame(loop, (uv_handle_t*) handle);
    }
  }
}


void uv_overlapped_endgame(uv_loop_t* loop, uv_overlapped_t* handle) {
  assert(handle->flags & UV__HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  assert(handle->overlapped_cb == NULL);

  uv__handle_close(handle);
}


void uv_process_overlapped_req(uv_loop_t* loop, uv_overlapped_t* handle) {
  assert(handle->type == UV_OVERLAPPED);
  assert((handle->flags & UV__HANDLE_CLOSING) || uv__is_active(handle));
  assert(handle->overlapped_cb != NULL);

  uv_overlapped_cb overlapped_cb = handle->overlapped_cb;
  handle->overlapped_cb = NULL;

  if ((handle->flags & UV__HANDLE_CLOSING)) {
    uv_want_endgame(loop, (uv_handle_t*) handle);
    return;
  }

  uv__handle_stop(handle);

  overlapped_cb(handle);
}


