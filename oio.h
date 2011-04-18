#ifndef OIO_H
#define OIO_H

#define OIO_VERSION_MAJOR 0
#define OIO_VERSION_MINOR 1

#include <stdint.h> /* int64_t */
#include <stddef.h> /* size_t */


typedef int oio_err; /* FIXME */

typedef struct oio_req_s oio_req;
typedef struct oio_handle_s oio_handle;

/* TODO: tell the callback if the request was completed or cancelled */
typedef void (*oio_read_cb)(oio_req* req, size_t nread);
typedef void (*oio_write_cb)(oio_req* req);
typedef void (*oio_accept_cb)(oio_handle* handle);
typedef void (*oio_close_cb)(oio_handle* handle, oio_err e);
typedef void (*oio_connect_cb)(oio_req* req, oio_err e);
typedef void (*oio_shutdown_cb)(oio_req* req);
typedef void (*oio_timer_cb)(oio_req* req, int64_t skew);


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


/**
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call oio_last_error() to determine
 * the error code.
 */
oio_err oio_last_error();
const char* oio_err_str(oio_err err);


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
int oio_tcp_handle_init(oio_handle *handle, oio_close_cb close_cb, void* data);
int oio_bind(oio_handle* handle, struct sockaddr* addr);
int oio_connect(oio_req* req, struct sockaddr* addr);
int oio_shutdown(oio_req* req);

/* TCP server methods. */
int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb);
int oio_tcp_handle_accept(oio_handle* server, oio_handle* client,
                          oio_close_cb close_cb, void* data);

/* Generic handle methods */
int oio_read(oio_req* req, oio_buf* bufs, int bufcnt);
int oio_write(oio_req* req, oio_buf* bufs, int bufcnt);
int oio_write2(oio_req *req, const char* msg);

/* Timer methods */
int oio_timeout(oio_req *req, int64_t timeout);

/* Request handle to be closed. close_cb will be called */
/* asynchronously after this call. */
int oio_close(oio_handle* handle);


/* Utility */
struct sockaddr_in oio_ip4_addr(char* ip, int port);

#endif /* OIO_H */
