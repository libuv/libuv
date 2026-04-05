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


int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb) {
  int err;
  if (uv__is_closing(stream)) {
    return UV_EINVAL;
  }
  err = ERROR_INVALID_PARAMETER;
  switch (stream->type) {
    case UV_TCP:
      err = uv__tcp_listen((uv_tcp_t*)stream, backlog, cb);
      break;
    case UV_NAMED_PIPE:
      err = uv__pipe_listen((uv_pipe_t*)stream, backlog, cb);
      break;
    default:
      assert(0);
  }

  return uv_translate_sys_error(err);
}


int uv_accept(uv_stream_t* server, uv_stream_t* client) {
  int err;

  err = ERROR_INVALID_PARAMETER;
  switch (server->type) {
    case UV_TCP:
      err = uv__tcp_accept((uv_tcp_t*)server, (uv_tcp_t*)client);
      break;
    case UV_NAMED_PIPE:
      err = uv__pipe_accept((uv_pipe_t*)server, client);
      break;
    default:
      assert(0);
  }

  return uv_translate_sys_error(err);
}


int uv__read_start(uv_stream_t* handle,
                   uv_alloc_cb alloc_cb,
                   uv_read_cb read_cb) {
  int err;

  err = ERROR_INVALID_PARAMETER;
  switch (handle->type) {
    case UV_TCP:
      err = uv__tcp_read_start((uv_tcp_t*)handle, alloc_cb, read_cb);
      break;
    case UV_NAMED_PIPE:
      err = uv__pipe_read_start((uv_pipe_t*)handle, alloc_cb, read_cb);
      break;
    case UV_TTY:
      err = uv__tty_read_start((uv_tty_t*) handle, alloc_cb, read_cb);
      break;
    default:
      assert(0);
  }

  return uv_translate_sys_error(err);
}


int uv_read_stop(uv_stream_t* handle) {
  int err;

  if (!(handle->flags & UV_HANDLE_READING))
    return 0;

  err = 0;
  if (handle->type == UV_TTY) {
    err = uv__tty_read_stop((uv_tty_t*) handle);
  } else if (handle->type == UV_NAMED_PIPE) {
    uv__pipe_read_stop((uv_pipe_t*) handle);
  } else {
    handle->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(handle->loop, handle);
  }

  return uv_translate_sys_error(err);
}


int uv_write(uv_write_t* req,
             uv_stream_t* handle,
             const uv_buf_t bufs[],
             unsigned int nbufs,
             uv_write_cb cb) {
  uv_loop_t* loop = handle->loop;
  int err;

  if (!(handle->flags & UV_HANDLE_WRITABLE)) {
    return UV_EPIPE;
  }

  err = ERROR_INVALID_PARAMETER;
  switch (handle->type) {
    case UV_TCP:
      err = uv__tcp_write(loop, req, (uv_tcp_t*) handle, bufs, nbufs, cb);
      break;
    case UV_NAMED_PIPE:
      err = uv__pipe_write(
          loop, req, (uv_pipe_t*) handle, bufs, nbufs, NULL, cb);
      return uv_translate_write_sys_error(err);
    case UV_TTY:
      err = uv__tty_write(loop, req, (uv_tty_t*) handle, bufs, nbufs, cb);
      break;
    default:
      assert(0);
  }

  return uv_translate_sys_error(err);
}


int uv_write2(uv_write_t* req,
              uv_stream_t* handle,
              const uv_buf_t bufs[],
              unsigned int nbufs,
              uv_stream_t* send_handle,
              uv_write_cb cb) {
  uv_loop_t* loop = handle->loop;
  int err;

  if (send_handle == NULL) {
    return uv_write(req, handle, bufs, nbufs, cb);
  }

  if (handle->type != UV_NAMED_PIPE || !((uv_pipe_t*) handle)->ipc) {
    return UV_EINVAL;
  } else if (!(handle->flags & UV_HANDLE_WRITABLE)) {
    return UV_EPIPE;
  }

  err = uv__pipe_write(
      loop, req, (uv_pipe_t*) handle, bufs, nbufs, send_handle, cb);
  return uv_translate_write_sys_error(err);
}


int uv_try_write(uv_stream_t* stream,
                 const uv_buf_t bufs[],
                 unsigned int nbufs) {
  if (stream->flags & UV_HANDLE_CLOSING)
    return UV_EBADF;
  if (!(stream->flags & UV_HANDLE_WRITABLE))
    return UV_EPIPE;

  switch (stream->type) {
    case UV_TCP:
      return uv__tcp_try_write((uv_tcp_t*) stream, bufs, nbufs);
    case UV_TTY:
      return uv__tty_try_write((uv_tty_t*) stream, bufs, nbufs);
    case UV_NAMED_PIPE:
      return UV_EAGAIN;
    default:
      assert(0);
      return UV_ENOSYS;
  }
}


int uv_try_write2(uv_stream_t* stream,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  uv_stream_t* send_handle) {
  if (send_handle != NULL)
    return UV_EAGAIN;
  return uv_try_write(stream, bufs, nbufs);
}


int uv_shutdown(uv_shutdown_t* req, uv_stream_t* handle, uv_shutdown_cb cb) {
  uv_loop_t* loop = handle->loop;

  if (!(handle->flags & UV_HANDLE_WRITABLE) ||
      uv__is_stream_shutting(handle) ||
      uv__is_closing(handle)) {
    return UV_ENOTCONN;
  }

  UV_REQ_INIT(req, UV_SHUTDOWN);
  req->handle = handle;
  req->cb = cb;

  handle->flags &= ~UV_HANDLE_WRITABLE;
  handle->stream.conn.shutdown_req = req;
  handle->reqs_pending++;
  REGISTER_HANDLE_REQ(loop, handle);

  if (handle->stream.conn.write_reqs_pending == 0) {
    if (handle->type == UV_NAMED_PIPE)
      uv__pipe_shutdown(loop, (uv_pipe_t*) handle, req);
    else
      uv__insert_pending_req(loop, (uv_req_t*) req);
  }

  return 0;
}


int uv_is_readable(const uv_stream_t* handle) {
  return !!(handle->flags & UV_HANDLE_READABLE);
}


int uv_is_writable(const uv_stream_t* handle) {
  return !!(handle->flags & UV_HANDLE_WRITABLE);
}


int uv_stream_set_blocking(uv_stream_t* handle, int blocking) {
  if (handle->type != UV_NAMED_PIPE)
    return UV_EINVAL;

  if (blocking != 0)
    handle->flags |= UV_HANDLE_BLOCKING_WRITES;
  else
    handle->flags &= ~UV_HANDLE_BLOCKING_WRITES;

  return 0;
}


int uv_tcp_export(uv_tcp_t* stream, int* fd) {
  WSAPROTOCOL_INFOW protocol_info;
  SOCKET dup_socket;

  /* Set a safe sentinel so callers don't observe garbage on any error path. */
  *fd = -1;

  if (stream->type != UV_TCP)
    return UV_EINVAL;

  /* A handle being closed (or already closed) has no usable socket.
   * UV_HANDLE_CLOSING is set by uv_close() before the close callback fires;
   * UV_HANDLE_CLOSED means teardown is complete.  Either way the socket is
   * gone or about to disappear — duplicating it would hand the caller a
   * socket that may be closed under them. */
  if (uv__is_closing((uv_handle_t*) stream))
    return UV_EINVAL;

  /* An initialised-but-not-yet-connected/bound TCP handle has
   * socket == INVALID_SOCKET.  Return EBADF explicitly rather than letting
   * WSADuplicateSocketW fail with an opaque Winsock error. */
  if (stream->socket == INVALID_SOCKET)
    return UV_EBADF;

  /* WSADuplicateSocketW does not preserve the kernel bind-for-listen state
   * for a socket that is bound but not yet listening.  The recipient calling
   * listen() on such a duplicate gets WSAEINVAL.  Calling listen() here
   * transitions the socket into LISTENING state before duplication so the
   * duplicate inherits it — mirroring what uv__tcp_xfer_export does.
   * Only do this for bound-but-not-yet-listening handles; sockets that are
   * already listening or are connection handles need no special treatment. */
  if ((stream->flags & UV_HANDLE_BOUND) &&
      !(stream->flags & UV_HANDLE_LISTENING) &&
      !(stream->flags & UV_HANDLE_CONNECTION)) {
    if (stream->delayed_error == 0 &&
        listen(stream->socket, SOMAXCONN) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
    stream->flags |= UV_HANDLE_LISTENING;
  }

  /* Duplicate the socket for the current process.
   * WSADuplicateSocketW fills in a WSAPROTOCOL_INFOW blob; WSASocketW then
   * creates a brand-new socket kernel object from that blob.  This is the
   * Windows equivalent of F_DUPFD on Unix: the caller gets an independent
   * socket that must be closed (or imported) separately. */
  if (WSADuplicateSocketW(stream->socket, GetCurrentProcessId(), &protocol_info))
    return uv_translate_sys_error(WSAGetLastError());

  /* WSA_FLAG_OVERLAPPED is required: libuv associates every socket it owns
   * with the IOCP loop; a socket created without this flag cannot do
   * overlapped I/O and will break uv_tcp_import.
   * WSA_FLAG_NO_HANDLE_INHERIT mirrors F_DUPFD_CLOEXEC on Unix: the
   * duplicated socket must not leak into child processes. */
  dup_socket = WSASocketW(FROM_PROTOCOL_INFO,
                          FROM_PROTOCOL_INFO,
                          FROM_PROTOCOL_INFO,
                          &protocol_info,
                          0,
                          WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (dup_socket == INVALID_SOCKET)
    return uv_translate_sys_error(WSAGetLastError());

  /* SOCKET is UINT_PTR (64-bit on x64), but Winsock2 guarantees that valid
   * socket values fit in 32 bits for compatibility with legacy 32-bit code,
   * so casting to int is safe.  The import side must reverse this with an
   * unsigned cast to avoid sign-extending a high-bit value. */
  *fd = (int) dup_socket;
  return 0;
}


int uv_tcp_import(uv_loop_t* loop, int fd, uv_tcp_t* out, unsigned int flags) {
  int err;

  err = uv_tcp_init_ex(loop, out, flags);
  if (err)
    return err;

  /* Reverse the cast done in uv_tcp_export: go through unsigned int first to
   * zero-extend to UINT_PTR.  A plain (SOCKET)(int) cast would sign-extend a
   * value whose bit 31 is set, producing a garbage 64-bit socket number. */
  err = uv_tcp_open(out, (uv_os_sock_t)(UINT_PTR)(unsigned int) fd);
  if (err) {
    uv_close((uv_handle_t*) out, NULL);
    return err;
  }

  return 0;
}
