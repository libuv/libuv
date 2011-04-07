#ifndef OIO_UNIX_H
#define OIO_UNIX_H

#include "ngx-queue.h"

#include "ev/ev.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


/**
 * Note can be cast to io_vec.
 */
typedef struct {
  char* base;
  size_t len;
} oio_buf;



typedef struct {
  int local;
  oio_connect_cb connect_cb;
  ngx_queue_t read_reqs;
  oio_buf* read_bufs;
  int read_bufcnt;
} oio_req_private;


typedef struct {
  int fd;

  oio_err err;

  oio_read_cb read_cb;
  oio_close_cb close_cb;

  oio_req *connect_req;

  ev_io read_watcher;
  ev_io write_watcher;

  ngx_queue_t write_queue;
  ngx_queue_t read_reqs;

} oio_handle_private;


#endif /* OIO_UNIX_H */
