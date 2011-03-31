#ifndef OL_H
#define OL_H

#include <stddef.h> /* size_t */



typedef int ol_err; /* FIXME */

typedef struct ol_req_s ol_req;
typedef struct ol_handle_s ol_handle;

typedef void (*ol_read_cb)(ol_req* req, size_t nread);
typedef void (*ol_write_cb)(ol_req* req);
typedef void (*ol_accept_cb)(ol_handle* server, ol_handle* new_client);
typedef void (*ol_close_cb)(ol_handle* handle, ol_err e);
typedef void (*ol_connect_cb)(ol_req* req, ol_err e);
typedef void (*ol_shutdown_cb)(ol_req* req);


#if defined(__unix__) || defined(__POSIX__)
# include "ol-unix.h"
#else
# include "ol-win.h"
#endif


typedef enum {
  OL_UNKNOWN_HANDLE = 0,
  OL_TCP,
  OL_NAMED_PIPE,
  OL_TTY,
  OL_FILE,
} ol_handle_type;


struct ol_handle_s {
  /* read-only */
  ol_handle_type type;
  /* private */
  ol_handle_private _;
  /* public */
  ol_accept_cb accept_cb;
  ol_close_cb close_cb;
  void* data;
};


typedef enum {
  OL_UNKNOWN_REQ = 0,
  OL_CONNECT,
  OL_ACCEPT,
  OL_READ,
  OL_WRITE,
  OL_SHUTDOWN
} ol_req_type;


struct ol_req_s {
  /* read-only */
  ol_req_type type;
  ol_handle* handle;
  /* private */
  ol_req_private _;
  /* public */
  void* cb;
  void *data;
};


/**
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call ol_last_error() to determine
 * the error code.
 */
ol_err ol_last_error();
const char* ol_err_str(ol_err err);


void ol_init();
int ol_run();

void ol_req_init(ol_req* req, void* cb);

ol_handle* ol_tcp_handle_new(ol_close_cb close_cb, void* data);
/* TODO:
 * ol_named_pipe_handle_new
 * ol_file_handle_new
 * ol_tty_handle_new
 */

/* TCP server methods. */
int ol_bind(ol_handle* handle, struct sockaddr* addr);
int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb);

/* TCP socket methods. */
int ol_connect(ol_handle* handle, ol_req *req, struct sockaddr* addr);
int ol_read(ol_handle* handle, ol_req *req, ol_buf* bufs, int bufcnt);
int ol_write(ol_handle* handle, ol_req *req, ol_buf* bufs, int bufcnt);
int ol_write2(ol_handle* handle, const char* msg);
int ol_shutdown(ol_handle* handle, ol_req *req);

/* Request handle to be closed. close_cb will be made */
/* synchronously during this call. */
int ol_close(ol_handle* handle);

/* Must be called for all handles after close_cb. Handles that arrive
 * via the accept_cb must use ol_free().
 */
void ol_free(ol_handle* handle);



/* Utility */
struct sockaddr_in ol_ip4_addr(char *ip, int port);

#endif /* OL_H */
