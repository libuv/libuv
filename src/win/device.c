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
#include <stdlib.h>
#include <fcntl.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "stream-inl.h"
#include "req-inl.h"

/* A zero-size buffer for use by uv_device_read */
static char uv_zero_[] = "";

int uv_device_init(uv_loop_t* loop, 
                   uv_device_t* handle,
                   const char* path,
                   int flags) {
  DWORD dwCreationDisposition;
  int uvflags;

  assert(handle);
  if (flags != O_RDONLY && flags != O_WRONLY && flags != O_RDWR)
    return -EINVAL;

  dwCreationDisposition = 0;
  uvflags = 0; 

  uv_stream_init(loop, (uv_stream_t*) handle, UV_DEVICE);
  handle->handle = INVALID_HANDLE_VALUE;
  handle->read_buffer.base = NULL;
  handle->read_buffer.len = 0;

  if (flags & O_RDONLY) {
    dwCreationDisposition |= GENERIC_READ;
    uvflags |= UV_HANDLE_READABLE;
  } else if (flags & O_WRONLY) {
    dwCreationDisposition |= GENERIC_WRITE;
    uvflags |= UV_HANDLE_WRITABLE;
  } else if (flags & O_RDWR) {
    dwCreationDisposition |= (GENERIC_READ | GENERIC_WRITE);
    uvflags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
  }

  handle->handle = CreateFile(path,
                              dwCreationDisposition,
                              0,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
                              0);
  if (handle->handle == INVALID_HANDLE_VALUE)
    return uv_translate_sys_error(GetLastError());

  /* Try to associate with IOCP. */
  if (!CreateIoCompletionPort(handle->handle,
                              loop->iocp,
                              (ULONG_PTR)handle,
                              0)) {
    DWORD err = GetLastError();
    if (err) {
      return uv_translate_sys_error(err);
    }
  }

  handle->flags |= uvflags;
  uv_connection_init((uv_stream_t*) handle);
  return 0;
}

int uv_device_ioctl(uv_device_t* device, int cmd, uv_ioargs_t* args) {
  BOOL r;
  DWORD size = 0;
  assert(device && device->handle != INVALID_HANDLE_VALUE);
  assert(args && args->input && args->input_len && args->output_len);

  r = DeviceIoControl(device->handle, 
                      (DWORD) cmd,
                      args->input,
                      args->input_len,
                      args->output,
                      args->output_len,
                      &size,
                      NULL);

  if (r) 
    return size;

  return uv_translate_sys_error(GetLastError());
}

void uv_device_endgame(uv_loop_t* loop, uv_device_t* handle) {
  uv__handle_close(handle);
}

static void uv_device_queue_read(uv_loop_t* loop, uv_device_t* handle) {
  uv_read_t* req;
  BOOL r;
  DWORD err;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));
  assert(handle->handle && handle->handle != INVALID_HANDLE_VALUE);

  req = &handle->read_req;
  memset(&req->overlapped, 0, sizeof(req->overlapped));
  handle->alloc_cb((uv_handle_t*) handle, 65536, &handle->read_buffer);
  if (handle->read_buffer.len == 0) {
    handle->read_cb((uv_stream_t*) handle, UV_ENOBUFS, &handle->read_buffer);
    return;
  }

  r = ReadFile(handle->handle,
               handle->read_buffer.base,
               handle->read_buffer.len,
               NULL,
               &req->overlapped);

  if (r) {
    handle->flags |= UV_HANDLE_READ_PENDING;
    handle->reqs_pending++;
    uv_insert_pending_req(loop, (uv_req_t*) req);
  } else {
    err = GetLastError();
    if (r == 0 && err == ERROR_IO_PENDING) {
      /* The req will be processed with IOCP. */
      handle->flags |= UV_HANDLE_READ_PENDING;
      handle->reqs_pending++;
    } else {
      /* Make this req pending reporting an error. */
      SET_REQ_ERROR(req, err);
      uv_insert_pending_req(loop, (uv_req_t*) req);
      handle->reqs_pending++;
    }
  } 
};

int uv_device_read_start(uv_device_t* handle,
                         uv_alloc_cb alloc_cb,
                         uv_read_cb read_cb) {
  uv_loop_t* loop = handle->loop;

  if (!(handle->flags & UV_HANDLE_READABLE))
    return ERROR_INVALID_PARAMETER;

  handle->flags |= UV_HANDLE_READING;
  INCREASE_ACTIVE_COUNT(loop, handle);
  handle->read_cb = read_cb;
  handle->alloc_cb = alloc_cb;

  /* If reading was stopped and then started again, there could still be a */
  /* read request pending. */
  if (handle->flags & UV_HANDLE_READ_PENDING)
    return 0;

  uv_device_queue_read(loop, handle);
  return 0; 
}


int uv_device_write(uv_loop_t* loop,
                    uv_write_t* req,
                    uv_device_t* handle,
                    const uv_buf_t bufs[],
                    unsigned int nbufs,
                    uv_write_cb cb) {
  int result;
  DWORD err = 0;
  
  if (nbufs != 1 && (nbufs != 0 || !handle)) {
    return ERROR_NOT_SUPPORTED;
  }
  /* Only TCP handles are supported for sharing. */
  if (handle && (!(handle->flags & UV_HANDLE_CONNECTION))) {
    return ERROR_NOT_SUPPORTED;
  }

  uv_req_init(loop, (uv_req_t*) req);
  req->type = UV_WRITE;
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  req->queued_bytes = 0;
  memset(&(req->overlapped), 0, sizeof(req->overlapped));

  result = WriteFile(handle->handle,
                     bufs[0].base,
                     bufs[0].len,
                     NULL,
                     &req->overlapped);

  if (result) {
    /* Request completed immediately. */
    req->queued_bytes = 0;
    handle->reqs_pending++;
    handle->write_reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle, req);
    uv_insert_pending_req(loop, (uv_req_t*) req);
  } else {
    /* Request queued by the kernel. */
    err = GetLastError();
    if (err != ERROR_IO_PENDING)
      return GetLastError();

    req->queued_bytes = uv__count_bufs(bufs, nbufs);
    handle->reqs_pending++;
    handle->write_reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle, req);
    handle->write_queue_size += req->queued_bytes;
  }
  return 0;
}

void uv_process_device_read_req(uv_loop_t* loop,
                                uv_device_t* handle,
                                uv_req_t* req) {
  DWORD err;

  assert(handle->type == UV_DEVICE);
  assert(handle->flags & UV_HANDLE_READING);

  handle->flags &= ~UV_HANDLE_READ_PENDING;

  if (!REQ_SUCCESS(req)) {
    /* An error occurred doing the read. */
    if (handle->flags & UV_HANDLE_READING)  {
      handle->flags &= ~UV_HANDLE_READING;
      DECREASE_ACTIVE_COUNT(loop, handle);

      err = GET_REQ_SOCK_ERROR(req);
      handle->read_cb((uv_stream_t*) handle,
                      uv_translate_sys_error(err),
                      &handle->read_buffer);
    }
  } else {
    if (req->overlapped.InternalHigh > 0) {
      /* Successful read */
      handle->read_cb((uv_stream_t*) handle,
                      req->overlapped.InternalHigh,
                      &handle->read_buffer);
      /* Read again only if bytes == buf.len */
      if (req->overlapped.InternalHigh < sizeof(handle->read_buffer.len))
        goto done;
    } else {
      /* Connection closed */
      if (handle->flags & UV_HANDLE_READING) {
        handle->flags &= ~UV_HANDLE_READING;
        DECREASE_ACTIVE_COUNT(loop, handle);
      }
      handle->flags &= ~UV_HANDLE_READABLE;

      handle->read_cb((uv_stream_t*) handle, UV_EOF, &handle->read_buffer);
      goto done;
    }
  }
  /* 
    may be we need more do read operation 
  */
done:
  /* Post another read if still reading and not closing. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
      uv_device_queue_read(loop, handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv_process_device_write_req(uv_loop_t* loop,
                                 uv_device_t* handle,
                                 uv_write_t* req) {
  int err;

  assert(handle->type == UV_DEVICE);
  assert(handle->write_queue_size >= req->queued_bytes);
  handle->write_queue_size -= req->queued_bytes;

  UNREGISTER_HANDLE_REQ(loop, handle, req);

  if (req->cb) {
    err = GET_REQ_ERROR(req);
    req->cb(req, uv_translate_sys_error(err));
  }

  handle->write_reqs_pending--;

  if (handle->shutdown_req != NULL &&
      handle->write_reqs_pending == 0) {
      uv_want_endgame(loop, (uv_handle_t*) handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv_device_close(uv_loop_t* loop, uv_device_t* device) {
  if (device->flags & UV_HANDLE_READING) {
    device->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, device);
  }

  CloseHandle(device->handle);
  device->handle = INVALID_HANDLE_VALUE;

  device->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  uv__handle_closing(device);

  if (device->reqs_pending == 0)
    uv_want_endgame(device->loop, (uv_handle_t*) device);
}

/* this should never be called */
void uv_process_device_accept_req(uv_loop_t* loop, 
                                  uv_device_t* handle,
                                  uv_req_t* raw_req) {
    abort();
}

/* this should never be called */
void uv_process_device_connect_req(uv_loop_t* loop, 
                                   uv_device_t* handle,
                                   uv_connect_t* req) {
    abort();
}
