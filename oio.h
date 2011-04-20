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
#include <stddef.h> /* size_t */


typedef struct oio_handle_s oio_handle;
typedef struct oio_req_s oio_req;
typedef struct oio_err_s oio_err;

/**
 * The status parameter is 0 if the request completed successfully,
 * and should be -1 if the request was cancelled or failed.
 * For oio_close_cb, -1 means that the handle was closed due to an error.
 * Error details can be obtained by calling oio_last_error().
 */
typedef void (*oio_read_cb)(oio_req* req, size_t nread, int status);
typedef void (*oio_write_cb)(oio_req* req, int status);
typedef void (*oio_connect_cb)(oio_req* req, int status);
typedef void (*oio_accept_cb)(oio_handle* handle);
typedef void (*oio_close_cb)(oio_handle* handle, int status);
typedef void (*oio_timer_cb)(oio_req* req, int64_t skew, int status);


#if defined(__unix__) || defined(__POSIX__) || defined(__APPLE__)
# include "oio-unix.h"
#else
# include "oio-win.h"
#endif


typedef enum {
  OIO_UNKNOWN_HANDLE = 0,
  OIO_TCP,
  OIO_NAMED_PIPE,
  OIO_TTY,
  OIO_FILE,
} oio_handle_type;

typedef enum {
  OIO_UNKNOWN_REQ = 0,
  OIO_CONNECT,
  OIO_ACCEPT,
  OIO_READ,
  OIO_WRITE,
  OIO_SHUTDOWN,
  OIO_CLOSE,
  OIO_TIMEOUT
} oio_req_type;

/* Expand this list if necessary. */
typedef enum {
  OIO_UNKNOWN = -1,
  OIO_OK = 0,
  OIO_EACCESS,
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


struct oio_handle_s {
  /* read-only */
  oio_handle_type type;
  /* public */
  oio_close_cb close_cb;
  void* data;
  /* private */
  oio_handle_private_fields
};

struct oio_req_s {
  /* read-only */
  oio_req_type type;
  /* public */
  oio_handle* handle;
  void* cb;
  void* data;
  /* private */
  oio_req_private_fields
};

struct oio_err_s {
  /* read-only */
  oio_err_code code;
  /* private */
  int sys_errno_;
};


/**
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call oio_last_error() to determine
 * the error code.
 */
oio_err oio_last_error();
char* oio_strerror(oio_err err);


void oio_init();
int oio_run();

void oio_update_time();
int64_t oio_now();

void oio_req_init(oio_req* req, oio_handle* handle, void* cb);

/*
 * TODO:
 * - oio_(pipe|pipe_tty)_handle_init
 * - oio_bind_pipe(char *name)
 * - oio_continuous_read(oio_handle *handle, oio_continuous_read_cb *cb)
 * - A way to list cancelled oio_reqs after before/on oio_close_cb
 */

/* TCP socket methods. */
/* Handle and callback bust be set by calling oio_req_init. */
int oio_tcp_init(oio_handle *handle, oio_close_cb close_cb, void* data);
int oio_bind(oio_handle* handle, struct sockaddr* addr);
int oio_connect(oio_req* req, struct sockaddr* addr);
int oio_shutdown(oio_req* req);

/* TCP server methods. */
int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb);

/* Call this after accept_cb. client does not need to be initialized. */
int oio_accept(oio_handle* server, oio_handle* client,
    oio_close_cb close_cb, void* data);

/* Generic read/write methods. */
/* The buffers to be written or read into must remain valid until the */
/* callback is called. The oio_buf array does need not remain valid! */
int oio_read(oio_req* req, oio_buf* bufs, int bufcnt);
int oio_write(oio_req* req, oio_buf* bufs, int bufcnt);

/* Timer methods */
int oio_timeout(oio_req *req, int64_t timeout);

/* Request handle to be closed. close_cb will be called */
/* asynchronously after this call. */
int oio_close(oio_handle* handle);


/* Utility */
struct sockaddr_in oio_ip4_addr(char* ip, int port);

#endif /* OIO_H */
