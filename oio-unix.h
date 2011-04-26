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
  int write_index; \
  ev_timer timer; \
  ngx_queue_t read_reqs; \
  oio_buf* read_bufs; \
  int read_bufcnt;


#define oio_handle_private_fields \
  int fd; \
  int flags; \
  oio_read_cb read_cb; \
  oio_accept_cb accept_cb; \
  int accepted_fd; \
  oio_req *connect_req; \
  ev_io read_watcher; \
  ev_io write_watcher; \
  ev_idle next_watcher; \
  ngx_queue_t write_queue; \
  size_t write_queue_size; \
  ngx_queue_t read_reqs;


#endif /* OIO_UNIX_H */
