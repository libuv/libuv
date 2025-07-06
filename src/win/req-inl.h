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

#ifndef UV_WIN_REQ_INL_H_
#define UV_WIN_REQ_INL_H_

#include <assert.h>

#include "uv.h"
#include "internal.h"


#define SET_REQ_STATUS(req, status)                                     \
   (req)->u.io.overlapped.Internal = (ULONG_PTR) (status)

#define SET_REQ_ERROR(req, error)                                       \
  SET_REQ_STATUS((req), NTSTATUS_FROM_WIN32((error)))

/* Note: used open-coded in UV_REQ_INIT() because of a circular dependency
 * between src/uv-common.h and src/win/internal.h.
 */
#define SET_REQ_SUCCESS(req)                                            \
  SET_REQ_STATUS((req), STATUS_SUCCESS)

#define GET_REQ_STATUS(req)                                             \
  ((NTSTATUS) (req)->u.io.overlapped.Internal)

#define REQ_SUCCESS(req)                                                \
  (NT_SUCCESS(GET_REQ_STATUS((req))))

#define GET_REQ_ERROR(req)                                              \
  (pRtlNtStatusToDosError(GET_REQ_STATUS((req))))

#define GET_REQ_SOCK_ERROR(req)                                         \
  (uv__ntstatus_to_winsock_error(GET_REQ_STATUS((req))))


#define REGISTER_HANDLE_REQ(loop, handle)                               \
  do {                                                                  \
    INCREASE_ACTIVE_COUNT((loop), (handle));                            \
    uv__req_register((loop));                                           \
  } while (0)

#define UNREGISTER_HANDLE_REQ(loop, handle)                             \
  do {                                                                  \
    DECREASE_ACTIVE_COUNT((loop), (handle));                            \
    uv__req_unregister((loop));                                         \
  } while (0)


#define UV_SUCCEEDED_WITHOUT_IOCP(result)                               \
  ((result) && (handle->flags & UV_HANDLE_SYNC_BYPASS_IOCP))

#define UV_SUCCEEDED_WITH_IOCP(result)                                  \
  ((result) || (GetLastError() == ERROR_IO_PENDING))


#define POST_COMPLETION_FOR_REQ(loop, req)                              \
  if (!PostQueuedCompletionStatus((loop)->iocp,                         \
                                  0,                                    \
                                  0,                                    \
                                  &((req)->u.io.overlapped))) {         \
    uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");       \
  }

INLINE static void uv__insert_pending_req(uv_loop_t* loop, uv_req_t* req) {
  req->next_req = NULL;
  if (loop->pending_reqs_tail) {
#ifdef _DEBUG
    /* Ensure the request is not already in the queue, or the queue
     * will get corrupted.
     */
    uv_req_t* current = loop->pending_reqs_tail;
    do {
      assert(req != current);
      current = current->next_req;
    } while(current != loop->pending_reqs_tail);
#endif

    req->next_req = loop->pending_reqs_tail->next_req;
    loop->pending_reqs_tail->next_req = req;
    loop->pending_reqs_tail = req;
  } else {
    req->next_req = req;
    loop->pending_reqs_tail = req;
  }
}

#endif /* UV_WIN_REQ_INL_H_ */
