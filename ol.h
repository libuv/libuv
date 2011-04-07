#ifndef OL_H
#define OL_H

#include <stddef.h> /* size_t */


typedef int ol_err; /* FIXME */

typedef struct ol_req_s ol_req;
typedef struct ol_handle_s ol_handle;

typedef void (*ol_read_cb)(ol_req* req, size_t nread);
typedef void (*ol_write_cb)(ol_req* req);
typedef void (*ol_accept_cb)(ol_handle* handle);
typedef void (*ol_close_cb)(ol_handle* handle, ol_err e);
typedef void (*ol_connect_cb)(ol_req* req, ol_err e);
typedef void (*ol_shutdown_cb)(ol_req* req);


typedef enum {
  OL_UNKNOWN_HANDLE = 0,
  OL_TCP,
  OL_NAMED_PIPE,
  OL_TTY,
  OL_FILE,
} ol_handle_type;

typedef enum {
  OL_UNKNOWN_REQ = 0,
  OL_CONNECT,
  OL_ACCEPT,
  OL_READ,
  OL_WRITE,
  OL_SHUTDOWN,
  OL_CLOSE
} ol_req_type;


struct ol_handle_shared_s {
  /* read-only */
  ol_handle_type type;
  /* public */
  ol_close_cb close_cb;
  void* data;
};

struct ol_req_shared_s {
  /* read-only */
  ol_req_type type;
  /* public */
  ol_handle* handle;
  void* cb;
  void* data;
};


#if defined(__unix__) || defined(__POSIX__) || defined(__APPLE__)
# include "ol-unix.h"
#else
# include "ol-win.h"
#endif


/**
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call ol_last_error() to determine
 * the error code.
 */
ol_err ol_last_error();
const char* ol_err_str(ol_err err);


void ol_init();
int ol_run();

void ol_req_init(ol_req* req, ol_handle* handle, void* cb);

/* 
 * TODO:
 * - ol_(pipe|pipe_tty)_handle_init
 * - ol_bind_pipe(char *name)
 * - ol_continuous_read(ol_handle *handle, ol_continuous_read_cb *cb)
 * - A way to list cancelled ol_reqs after before/on ol_close_cb
 */

/* TCP socket methods. */
/* Handle and callback bust be set by calling ol_req_init. */
int ol_tcp_handle_init(ol_handle *handle, ol_close_cb close_cb, void* data);
int ol_bind(ol_handle* handle, struct sockaddr* addr);
int ol_connect(ol_req* req, struct sockaddr* addr);
int ol_shutdown(ol_req* req);

/* TCP server methods. */
int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb);
int ol_tcp_handle_accept(ol_handle* server, ol_handle* client, ol_close_cb close_cb, void* data);

/* Generic handle methods */
int ol_read(ol_req* req, ol_buf* bufs, int bufcnt);
int ol_write(ol_req* req, ol_buf* bufs, int bufcnt);
int ol_write2(ol_req *req, const char* msg);

/* Request handle to be closed. close_cb will be called */
/* asynchronously after this call. */
int ol_close(ol_handle* handle);


/* Utility */
struct sockaddr_in ol_ip4_addr(char* ip, int port);

#endif /* OL_H */
