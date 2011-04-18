#ifndef OIO_UNIX_H
#define OIO_UNIX_H

#include "ngx-queue.h"

#include "ev/ev.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


/* Note: May be cast to struct iovec. See writev(2). */
typedef struct {
  char* base;
  size_t len;
} oio_buf;


#define oio_req_private_fields \
  oio_connect_cb connect_cb; \
  ngx_queue_t read_reqs; \
  oio_buf* read_bufs; \
  ev_timer timer; \
  int read_bufcnt;


#define oio_handle_private_fields \
  int fd; \
  int flags; \
  oio_err err; \
  oio_read_cb read_cb; \
  oio_accept_cb accept_cb; \
  int accepted_fd; \
  oio_req *connect_req; \
  ev_io read_watcher; \
  ev_io write_watcher; \
  ev_idle next_watcher; \
  ngx_queue_t write_queue; \
  ngx_queue_t read_reqs;


#endif /* OIO_UNIX_H */
