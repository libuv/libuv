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

#ifndef OIO_H
#define OIO_H

#define OIO_VERSION_MAJOR 0
#define OIO_VERSION_MINOR 1

#include <stdint.h> /* int64_t */
#include <sys/types.h> /* size_t */

typedef struct oio_err_s oio_err;
typedef struct oio_handle_s oio_handle_t;
typedef struct oio_req_s oio_req_t;


#if defined(__unix__) || defined(__POSIX__) || defined(__APPLE__)
# include "oio-unix.h"
#else
# include "oio-win.h"
#endif


/* The status parameter is 0 if the request completed successfully,
 * and should be -1 if the request was cancelled or failed.
 * For oio_close_cb, -1 means that the handle was closed due to an error.
 * Error details can be obtained by calling oio_last_error().
 *
 * In the case of oio_read_cb the oio_buf returned should be freed by the
 * user.
 */
typedef oio_buf (*oio_alloc_cb)(oio_handle_t* handle, size_t suggested_size);
typedef void (*oio_read_cb)(oio_handle_t *handle, int nread, oio_buf buf);
typedef void (*oio_write_cb)(oio_req_t* req, int status);
typedef void (*oio_connect_cb)(oio_req_t* req, int status);
typedef void (*oio_shutdown_cb)(oio_req_t* req, int status);
typedef void (*oio_accept_cb)(oio_handle_t* handle);
typedef void (*oio_close_cb)(oio_handle_t* handle, int status);
typedef void (*oio_timer_cb)(oio_req_t* req, int64_t skew, int status);
typedef void (*oio_loop_cb)(oio_handle_t* handle, int status);


/* Expand this list if necessary. */
typedef enum {
  OIO_UNKNOWN = -1,
  OIO_OK = 0,
  OIO_EOF,
  OIO_EACCESS,
  OIO_EAGAIN,
  OIO_EADDRINUSE,
  OIO_EADDRNOTAVAIL,
  OIO_EAFNOSUPPORT,
  OIO_EALREADY,
  OIO_EBADF,
  OIO_EBUSY,
  OIO_ECONNABORTED,
  OIO_ECONNREFUSED,
  OIO_ECONNRESET,
  OIO_EDESTADDRREQ,
  OIO_EFAULT,
  OIO_EHOSTUNREACH,
  OIO_EINTR,
  OIO_EINVAL,
  OIO_EISCONN,
  OIO_EMFILE,
  OIO_ENETDOWN,
  OIO_ENETUNREACH,
  OIO_ENFILE,
  OIO_ENOBUFS,
  OIO_ENOMEM,
  OIO_ENONET,
  OIO_ENOPROTOOPT,
  OIO_ENOTCONN,
  OIO_ENOTSOCK,
  OIO_ENOTSUP,
  OIO_EPROTO,
  OIO_EPROTONOSUPPORT,
  OIO_EPROTOTYPE,
  OIO_ETIMEDOUT
} oio_err_code;

typedef enum {
  OIO_UNKNOWN_HANDLE = 0,
  OIO_TCP,
  OIO_NAMED_PIPE,
  OIO_TTY,
  OIO_FILE,
  OIO_PREPARE,
  OIO_CHECK,
  OIO_IDLE
} oio_handle_type;

typedef enum {
  OIO_UNKNOWN_REQ = 0,
  OIO_CONNECT,
  OIO_ACCEPT,
  OIO_READ,
  OIO_WRITE,
  OIO_SHUTDOWN,
  OIO_TIMEOUT
} oio_req_type;


struct oio_err_s {
  /* read-only */
  oio_err_code code;
  /* private */
  int sys_errno_;
};


struct oio_req_s {
  /* read-only */
  oio_req_type type;
  /* public */
  oio_handle_t* handle;
  void* cb;
  void* data;
  /* private */
  oio_req_private_fields
};


struct oio_handle_s {
  /* read-only */
  oio_handle_type type;
  /* public */
  oio_close_cb close_cb;
  void* data;
  /* private */
  oio_handle_private_fields
};


/* Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call oio_last_error() to determine
 * the error code.
 */
oio_err oio_last_error();
char* oio_strerror(oio_err err);


void oio_init(oio_alloc_cb alloc);
int oio_run();

/* Manually modify the event loop's reference count. Useful if the user wants
 * to have a handle or timeout that doesn't keep the loop alive.
 */
void oio_ref();
void oio_unref();

void oio_update_time();
int64_t oio_now();

void oio_req_init(oio_req_t* req, oio_handle_t* handle, void* cb);

/*
 * TODO:
 * - oio_(pipe|pipe_tty)_handle_init
 * - oio_bind_pipe(char* name)
 * - oio_continuous_read(oio_handle_t* handle, oio_continuous_read_cb* cb)
 * - A way to list cancelled oio_reqs after before/on oio_close_cb
 */

/* TCP socket methods.
 * Handle and callback bust be set by calling oio_req_init.
 */
int oio_tcp_init(oio_handle_t* handle, oio_close_cb close_cb, void* data);
int oio_bind(oio_handle_t* handle, struct sockaddr* addr);

int oio_connect(oio_req_t* req, struct sockaddr* addr);
int oio_shutdown(oio_req_t* req);

/* TCP server methods. */
int oio_listen(oio_handle_t* handle, int backlog, oio_accept_cb cb);

/* Call this after accept_cb. client does not need to be initialized. */
int oio_accept(oio_handle_t* server, oio_handle_t* client,
    oio_close_cb close_cb, void* data);


/* Read data from an incoming stream. The callback will be made several
 * several times until there is no more data to read or oio_read_stop is
 * called. When we've reached EOF nread will be set to -1 and the error is
 * set to OIO_EOF. When nread == -1 the buf parameter might not point to a
 * valid buffer; in that case buf.len and buf.base are both set to 0.
 * Note that nread might also be 0, which does *not* indicate an error or
 * eof; it happens when liboio requested a buffer through the alloc callback
 * but then decided that it didn't need that buffer.
 */
int oio_read_start(oio_handle_t* handle, oio_read_cb cb);
int oio_read_stop(oio_handle_t* handle);

int oio_write(oio_req_t* req, oio_buf bufs[], int bufcnt);

/* Timer methods */
int oio_timeout(oio_req_t* req, int64_t timeout);

/* Every active prepare handle gets its callback called exactly once per loop
 * iteration, just before the system blocks to wait for completed i/o.
 */
int oio_prepare_init(oio_handle_t* handle, oio_close_cb close_cb, void* data);
int oio_prepare_start(oio_handle_t* handle, oio_loop_cb cb);
int oio_prepare_stop(oio_handle_t* handle);

/* Every active check handle gets its callback called exactly once per loop
 * iteration, just after the system returns from blocking.
 */
int oio_check_init(oio_handle_t* handle, oio_close_cb close_cb, void* data);
int oio_check_start(oio_handle_t* handle, oio_loop_cb cb);
int oio_check_stop(oio_handle_t* handle);

/* Every active idle handle gets its callback called repeatedly until it is
 * stopped. This happens after all other types of callbacks are processed.
 * When there are multiple "idle" handles active, their callbacks are called
 * in turn.
 */
int oio_idle_init(oio_handle_t* handle, oio_close_cb close_cb, void* data);
int oio_idle_start(oio_handle_t* handle, oio_loop_cb cb);
int oio_idle_stop(oio_handle_t* handle);

/* Request handle to be closed. close_cb will be called
 * asynchronously after this call.
 */
int oio_close(oio_handle_t* handle);


/* Utility */
struct sockaddr_in oio_ip4_addr(char* ip, int port);

#endif /* OIO_H */
