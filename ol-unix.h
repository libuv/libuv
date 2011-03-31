#ifndef OL_UNIX_H
#define OL_UNIX_H

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
} ol_buf;



typedef struct {
  int local;
  ol_connect_cb connect_cb;
  ngx_queue_t read_reqs;
  ol_buf* read_bufs;
  int read_bufcnt;
} ol_req_private;


typedef struct {
  int fd;

  ol_err err;

  ol_read_cb read_cb;
  ol_close_cb close_cb;

  ol_req *connect_req;

  ev_io read_watcher;
  ev_io write_watcher;

  ngx_queue_t write_queue;
  ngx_queue_t read_reqs;

} ol_handle_private;


#endif /* OL_UNIX_H */
