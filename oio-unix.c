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

#include "oio.h"

#include <stdio.h> /* printf */

#include <stdlib.h>
#include <string.h> /* strerror */
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


static oio_err last_err;


void oio_tcp_io(EV_P_ ev_io* watcher, int revents);
void oio__next(EV_P_ ev_idle* watcher, int revents);
void oio_tcp_connect(oio_handle* handle);
int oio_tcp_open(oio_handle*, int fd);
void oio_finish_close(oio_handle* handle);


/* flags */
enum {
  OIO_CLOSING = 0x00000001,
  OIO_CLOSED  = 0x00000002
};


void oio_flag_set(oio_handle* handle, int flag) {
  handle->flags |= flag;
}


oio_err oio_last_error() {
  return last_err;
}


char* oio_strerror(oio_err err) {
  return strerror(err.sys_errno_);
}


void oio_flag_unset(oio_handle* handle, int flag) {
  handle->flags = handle->flags & ~flag;
}


int oio_flag_is_set(oio_handle* handle, int flag) {
  return (handle->flags & flag) != 0;
}


static oio_err_code oio_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case 0: return OIO_OK;
    case EMFILE: return OIO_EMFILE;
    case EINVAL: return OIO_EINVAL;
    case ECONNREFUSED: return OIO_ECONNREFUSED;
    case EADDRINUSE: return OIO_EADDRINUSE;
    default: return OIO_UNKNOWN;
  }
}


static oio_err oio_err_new(oio_handle* handle, int sys_error) {
  oio_err err;
  err.sys_errno_ = sys_error;
  err.code = oio_translate_sys_error(sys_error);
  last_err = err;
  return err;
}


oio_err oio_err_last(oio_handle* handle) {
  return handle->err;
}


struct sockaddr_in oio_ip4_addr(char* ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int oio_close(oio_handle* handle) {
  oio_flag_set(handle, OIO_CLOSING);

  ev_io_stop(EV_DEFAULT_ &handle->write_watcher);
  ev_io_stop(EV_DEFAULT_ &handle->read_watcher);

  ev_idle_start(EV_DEFAULT_ &handle->next_watcher);
  ev_feed_event(EV_DEFAULT_ &handle->next_watcher, EV_IDLE);
  assert(ev_is_pending(&handle->next_watcher));

  return 0;
}


void oio_init() {
  ev_default_loop(0);
}


int oio_run() {
  ev_run(EV_DEFAULT_ 0);
}


int oio_tcp_init(oio_handle* handle, oio_close_cb close_cb,
    void* data) {
  handle->type = OIO_TCP;
  handle->close_cb = close_cb;
  handle->data = data;
  handle->flags = 0;
  handle->connect_req = NULL;
  handle->accepted_fd = -1;
  handle->fd = -1;

  ngx_queue_init(&handle->read_reqs);

  ev_init(&handle->next_watcher, oio__next);
  handle->next_watcher.data = handle;

  ev_init(&handle->read_watcher, oio_tcp_io);
  handle->read_watcher.data = handle;

  ev_init(&handle->write_watcher, oio_tcp_io);
  handle->write_watcher.data = handle;

  return 0;
}


int oio_bind(oio_handle* handle, struct sockaddr* addr) {
  int addrsize;
  int domain;
  int r;

  if (handle->fd <= 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      oio_err_new(handle, errno);
      return -1;
    }

    if (oio_tcp_open(handle, fd)) {
      close(fd);
      return -2;
    }
  }

  assert(handle->fd >= 0);

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
    domain = AF_INET;
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
    domain = AF_INET6;
  } else {
    assert(0);
    return -1;
  }

  r = bind(handle->fd, addr, addrsize);
  oio_err_new(handle, errno);
  return r;
}


int oio_tcp_open(oio_handle* handle, int fd) {
  assert(fd >= 0);
  handle->fd = fd;

  /* Set non-blocking. */
  int yes = 1;
  int r = fcntl(fd, F_SETFL, O_NONBLOCK);
  assert(r == 0);

  /* Reuse the port address. */
  r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(r == 0);

  /* Initialize the queue structure for oio_read() requests. */
  ngx_queue_init(&handle->read_reqs);

  /* Associate the fd with each ev_io watcher. */
  ev_io_set(&handle->read_watcher, fd, EV_READ);
  ev_io_set(&handle->write_watcher, fd, EV_WRITE);

  /* These should have been set up by oio_tcp_init. */
  assert(handle->next_watcher.data == handle);
  assert(handle->write_watcher.data == handle);
  assert(handle->read_watcher.data == handle);

  return 0;
}


void oio__server_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;
  assert(watcher == &handle->read_watcher ||
         watcher == &handle->write_watcher);
  assert(revents == EV_READ);

  assert(!oio_flag_is_set(handle, OIO_CLOSING));

  if (handle->accepted_fd >= 0) {
    ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
    return;
  }

  while (1) {
    struct sockaddr addr = { 0 };
    socklen_t addrlen = 0;

    assert(handle->accepted_fd < 0);
    int fd = accept(handle->fd, &addr, &addrlen);

    if (fd < 0) {
      if (errno == EAGAIN) {
        /* No problem. */
        return;
      } else if (errno == EMFILE) {
        /* TODO special trick. unlock reserved socket, accept, close. */
        return;
      } else {
        handle->err = oio_err_new(handle, errno);
        oio_close(handle);
      }

    } else {
      handle->accepted_fd = fd;
      handle->accept_cb(handle);
      if (handle->accepted_fd >= 0) {
        /* The user hasn't yet accepted called oio_accept() */
        ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
        return;
      }
    }
  }
}


int oio_accept(oio_handle* server, oio_handle* client,
    oio_close_cb close_cb, void* data) {
  if (server->accepted_fd < 0) {
    return -1;
  }

  if (oio_tcp_init(client, close_cb, data)) {
    return -1;
  }

  if (oio_tcp_open(client, server->accepted_fd)) {
    /* Ignore error for now */
    server->accepted_fd = -1;
    close(server->accepted_fd);
    return -1;
  } else {
    server->accepted_fd = -1;
    ev_io_start(EV_DEFAULT_ &server->read_watcher);
    return 0;
  }
}


int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb) {
  assert(handle->fd >= 0);

  int r = listen(handle->fd, backlog);
  if (r < 0) {
    oio_err_new(handle, errno);
    return -1;
  }

  handle->accept_cb = cb;

  /* Start listening for connections. */
  ev_io_set(&handle->read_watcher, handle->fd, EV_READ);
  ev_set_cb(&handle->read_watcher, oio__server_io);
  ev_io_start(EV_DEFAULT_ &handle->read_watcher);

  return 0;
}


void oio_finish_close(oio_handle* handle) {
  assert(oio_flag_is_set(handle, OIO_CLOSING));
  assert(!oio_flag_is_set(handle, OIO_CLOSED));
  oio_flag_set(handle, OIO_CLOSED);

  ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
  ev_io_stop(EV_DEFAULT_ &handle->write_watcher);
  ev_idle_stop(EV_DEFAULT_ &handle->next_watcher);

  close(handle->fd);

  handle->fd = -1;

  if (handle->accepted_fd >= 0) {
    close(handle->accepted_fd);
    handle->accepted_fd = -1;
  }

  if (handle->close_cb) {
    handle->close_cb(handle, 0);
  }
}


oio_req* oio_read_reqs_head(oio_handle* handle) {
  ngx_queue_t* q = ngx_queue_head(&(handle->read_reqs));
  if (!q) {
    return NULL;
  }

  oio_req* req = ngx_queue_data(q, struct oio_req_s, read_reqs);
  assert(req);

  return req;
}


int oio_read_reqs_empty(oio_handle* handle) {
  return ngx_queue_empty(&(handle->read_reqs));
}


void oio__next(EV_P_ ev_idle* watcher, int revents) {
  oio_handle* handle = watcher->data;
  assert(watcher == &handle->next_watcher);
  assert(revents == EV_IDLE);

  /* For now this function is only to handle the closing event, but we might
   * put more stuff here later.
   */
  assert(oio_flag_is_set(handle, OIO_CLOSING));
  oio_finish_close(handle);
}


void oio__read(oio_handle* handle) {
  int errorno;
  assert(handle->fd >= 0);

  /* TODO: should probably while(1) here until EAGAIN */

  /* Get the request at the head of the read_reqs queue. */
  oio_req* req = oio_read_reqs_head(handle);
  if (!req) {
    ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
    return;
  }

  /* Cast to iovec. We had to have our own oio_buf instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  struct iovec* iov = (struct iovec*) req->read_bufs;
  int iovcnt = req->read_bufcnt;

  assert(iov);
  assert(iovcnt > 0);

  /* Now do the actual read. */

  ssize_t nread = readv(handle->fd, iov, iovcnt);
  errorno = errno;

  oio_read_cb cb = req->cb;

  if (nread < 0) {
    if (errorno == EAGAIN) {
      /* Just wait for the next one. */
      assert(ev_is_active(&handle->read_watcher));
      ev_io_start(EV_DEFAULT_ &handle->read_watcher);
    } else {
      oio_err err = oio_err_new(handle, errorno);
      if (cb) {
        cb(req, 0, -1);
      }
      handle->err = err;
      oio_close(handle);
    }
  } else {
    /* Successful read */

    /* First pop the req off handle->read_reqs */
    ngx_queue_remove(&req->read_reqs);

    free(req->read_bufs); /* FIXME: we should not be allocing for each read */
    req->read_bufs = NULL;

    /* NOTE: call callback AFTER freeing the request data. */
    if (cb) {
      cb(req, nread, 0);
    }

    if (oio_read_reqs_empty(handle)) {
      ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
    }
  }
}


void oio_tcp_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;
  assert(watcher == &handle->read_watcher ||
         watcher == &handle->write_watcher);

  assert(handle->fd >= 0);

  assert(!oio_flag_is_set(handle, OIO_CLOSING));

  if (handle->connect_req) {
    oio_tcp_connect(handle);
  } else {
    if (revents & EV_READ) {
      oio__read(handle);
    }

    if (revents & EV_WRITE) {
      /* ignore for now */
      ev_io_stop(EV_DEFAULT_ &handle->write_watcher);
    }
  }
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
void oio_tcp_connect(oio_handle* handle) {
  assert(handle->fd >= 0);

  oio_req* req = handle->connect_req;
  assert(req);

  int error;
  socklen_t errorsize = sizeof(int);
  getsockopt(handle->fd, SOL_SOCKET, SO_ERROR, &error, &errorsize);

  if (!error) {
    ev_io_start(EV_DEFAULT_ &handle->read_watcher);

    /* Successful connection */
    handle->connect_req = NULL;
    oio_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      connect_cb(req, 0);
    }

  } else if (error == EINPROGRESS) {
    /* Still connecting. */
    return;

  } else {
    oio_err err = oio_err_new(handle, error);

    oio_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      connect_cb(req, -1);
    }

    handle->err = err;
    oio_close(handle);
  }
}


int oio_connect(oio_req* req, struct sockaddr* addr) {
  oio_handle* handle = req->handle;

  if (handle->fd <= 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      oio_err_new(handle, errno);
      return -1;
    }

    if (oio_tcp_open(handle, fd)) {
      close(fd);
      return -2;
    }
  }

  req->type = OIO_CONNECT;
  ngx_queue_init(&req->read_reqs);

  if (handle->connect_req) {
    oio_err_new(handle, EALREADY);
    return -1;
  }

  if (handle->type != OIO_TCP) {
    oio_err_new(handle, ENOTSOCK);
    return -1;
  }

  handle->connect_req = req;

  int addrsize = sizeof(struct sockaddr_in);

  int r = connect(handle->fd, addr, addrsize);
  if (r != 0 && errno != EINPROGRESS) {
    oio_err_new(handle, errno);
    return -1;
  }

  assert(handle->write_watcher.data == handle);
  ev_io_start(EV_DEFAULT_ &handle->write_watcher);

  return 0;
}


/* The buffers to be written must remain valid until the callback is called. */
/* This is not required for the oio_buf array. */
int oio_write(oio_req* req, oio_buf* bufs, int bufcnt) {
  oio_handle* handle = req->handle;
  assert(handle->fd >= 0);
  ssize_t r;
  int errorno;

  ngx_queue_init(&(req->read_reqs));
  req->type = OIO_WRITE;

  r = writev(handle->fd, (struct iovec*)bufs, bufcnt);
  errorno = errno;

  if (r < 0) {
    assert(errorno != EAGAIN && "write queueing not yet supported");
    oio_err_new(handle, errorno);
    return -1;
  } else {
    if (req && req->cb) {
      oio_write_cb cb = req->cb;
      cb(req, 0);
    }
    return 0;
  }
}


void oio__timeout(EV_P_ ev_timer* watcher, int revents) {
  oio_req* req = watcher->data;
  assert(watcher == &req->timer);
  assert(EV_TIMER & revents);

  /* This watcher is not repeating. */
  assert(!ev_is_active(watcher));
  assert(!ev_is_pending(watcher));

  if (req->cb) {
    oio_timer_cb cb = req->cb;
    /* TODO skew */
    cb(req, 0, 0);
  }
}


void oio_update_time() {
  ev_now_update(EV_DEFAULT_UC);
}


int64_t oio_now() {
  return (int64_t)(ev_now(EV_DEFAULT_UC) * 1000);
}


int oio_timeout(oio_req* req, int64_t timeout) {
  ev_timer_init(&req->timer, oio__timeout, timeout / 1000.0, 0.0);
  ev_timer_start(EV_DEFAULT_UC_ &req->timer);
  req->timer.data = req;
  return 0;
}


int oio_read(oio_req* req, oio_buf* bufs, int bufcnt) {
  oio_handle* handle = req->handle;
  ssize_t nread = -1;
  int errorno = EAGAIN;
  oio_read_cb cb = req->cb;

  assert(handle->fd >= 0);

  if (ngx_queue_empty(&handle->read_reqs)) {
    /* Attempt to read immediately. */
    nread = readv(handle->fd, (struct iovec*) bufs, bufcnt);
    errorno = errno;
  }

  /* The request should have been just initialized. Therefore the
   * ngx_queue_t for read_reqs should be empty.
   */
  assert(ngx_queue_empty(&req->read_reqs));
  assert(req->type == OIO_UNKNOWN_REQ);
  req->type = OIO_READ;

  if (nread < 0 && errorno != EAGAIN) {
    /* Real error. */
    oio_err err = oio_err_new(handle, errorno);

    if (cb) {
      cb(req, nread, -1);
    }

    return -1;
  }

  if (nread >= 0) {
    /* Successful read. */
    if (cb) {
      cb(req, nread, 0);
    }
    return 0;
  }

  /* Either we never read anything, or we got EAGAIN. */
  assert(!ngx_queue_empty(&handle->read_reqs) ||
         (nread < 0 && errorno == EAGAIN));

  /* Two possible states:
   * - EAGAIN, meaning the socket is not wriable currently. We must wait for
   *   it to become readable with the handle->read_watcher.
   * - The read_reqs queue already has reads. Meaning: the user has issued
   *   many oio_reads calls some of which are still waiting for the socket to
   *   become readable.
   * In the meantime we append the request to handle->read_reqs
   */

  /* Copy the bufs data over into our oio_req struct. This is so the user can
   * free the oio_buf array. The actual data inside the oio_bufs is however
   * owned by the user and cannot be deallocated until the read completes.
   *
   * TODO: instead of mallocing here - just have a fixed number of oio_bufs
   * included in the oio_req object.
   */
  req->read_bufs = malloc(sizeof(oio_buf) * bufcnt);
  memcpy(req->read_bufs, bufs, bufcnt * sizeof(oio_buf));
  req->read_bufcnt = bufcnt;

  /* Append the request to read_reqs. */
  ngx_queue_insert_tail(&handle->read_reqs, &req->read_reqs);

  assert(!ngx_queue_empty(&handle->read_reqs));
  assert(handle->read_watcher.data == handle);
  assert(handle->read_watcher.fd == handle->fd);

  ev_io_start(EV_DEFAULT_ &handle->read_watcher);

  return 0;
}


void oio_free(oio_handle* handle) {
  free(handle);
  /* lists? */
  return;
}


void oio_req_init(oio_req* req, oio_handle* handle, void* cb) {
  req->type = OIO_UNKNOWN_REQ;
  req->cb = cb;
  req->handle = handle;
  ngx_queue_init(&req->read_reqs);
}
