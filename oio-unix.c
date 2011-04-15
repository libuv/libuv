#include "oio.h"

#include <stdio.h> /* printf */

#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <string.h> /* strnlen */
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#ifndef strnlen
size_t strnlen (register const char* s, size_t maxlen) {
  register const char *e;
  size_t n;

  for (e = s, n = 0; *e && n < maxlen; e++, n++);
  return n;
}
#endif  /* strnlen */


void oio_tcp_io(EV_P_ ev_io* watcher, int revents);
void oio_tcp_connect(oio_handle* handle, oio_req* req);
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


void oio_flag_unset(oio_handle* handle, int flag) {
  handle->flags = handle->flags & ~flag;
}


int oio_flag_is_set(oio_handle* handle, int flag) {
  return handle->flags & flag;
}


static oio_err oio_err_new(oio_handle* handle, int e) {
  handle->err = e;
  return e;
}


oio_err oio_err_last(oio_handle *handle) {
  return handle->err;
}


struct sockaddr_in oio_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int oio_close(oio_handle* handle) {
  oio_flag_set(handle, OIO_CLOSING);

  if (!ev_is_active(&handle->read_watcher)) {
    ev_io_init(&handle->read_watcher, oio_tcp_io, handle->fd, EV_READ);
    ev_io_start(EV_DEFAULT_ &handle->read_watcher);
  }

  ev_feed_fd_event(EV_DEFAULT_ handle->fd, EV_READ | EV_WRITE);

  return 0;
}


void oio_init() {
  ev_default_loop(0);
}


int oio_run() {
  ev_run(EV_DEFAULT_ 0);
}


int oio_tcp_handle_init(oio_handle *handle, oio_close_cb close_cb,
    void* data) {
  handle->type = OIO_TCP;
  handle->close_cb = close_cb;
  handle->data = data;
  handle->accepted_fd = -1;
  handle->flags = 0;

  ngx_queue_init(&handle->read_reqs);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    oio_err_new(handle, errno);
    return -1;
  }

  if (oio_tcp_open(handle, fd)) {
    close(fd);
    return -2;
  }

  return 0;
}


int oio_bind(oio_handle* handle, struct sockaddr* addr) {
  int addrsize;
  int domain;
  int r;

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

  return oio_err_new(handle, r);
}


int oio_tcp_init_fd(int fd) {
  int r;
  int yes = 1;
  r = fcntl(fd, F_SETFL, O_NONBLOCK);
  assert(r == 0);
  r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(r == 0);
  return 0;
}


int oio_tcp_open(oio_handle* handle, int fd) {
  /* Set non-blocking, etc */
  oio_tcp_init_fd(fd);

  handle->fd = fd;

  ngx_queue_init(&handle->read_reqs);

  ev_io_init(&handle->read_watcher, oio_tcp_io, fd, EV_READ);
  ev_io_init(&handle->write_watcher, oio_tcp_io, fd, EV_WRITE);

  handle->read_watcher.data = handle;
  handle->write_watcher.data = handle;

  return 0;
}


void oio_server_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;

  assert(revents == EV_READ);

  if (oio_flag_is_set(handle, OIO_CLOSING)) {
    oio_finish_close(handle);
    return;
  }

  if (handle->accepted_fd >= 0) {
    ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
    return;
  }

  while (1) {
    struct sockaddr addr;
    socklen_t addrlen;

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
      if (!handle->accept_cb) {
        close(fd);
      } else {
        handle->accepted_fd = fd;
        handle->accept_cb(handle);
        if (handle->accepted_fd >= 0) {
          /* The user hasn't yet accepted called oio_tcp_handle_accept() */
          ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
          return;
        }
      }
    }
  }
}


int oio_tcp_handle_accept(oio_handle* server, oio_handle* client,
    oio_close_cb close_cb, void* data) {
  if (server->accepted_fd < 0) {
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
    return oio_err_new(handle, errno);
  }

  handle->accept_cb = cb;
  ev_io_init(&handle->read_watcher, oio_server_io, handle->fd, EV_READ);
  ev_io_start(EV_DEFAULT_ &handle->read_watcher);
  handle->read_watcher.data = handle;

  return 0;
}


void oio_finish_close(oio_handle* handle) {
  assert(!oio_flag_is_set(handle, OIO_CLOSED));
  oio_flag_set(handle, OIO_CLOSED);

  ev_io_stop(EV_DEFAULT_ &handle->read_watcher);
  ev_io_stop(EV_DEFAULT_ &handle->write_watcher);
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


void oio__read(oio_handle* handle) {
  assert(handle->fd >= 0);

  /* Get the request at the head of the read_reqs queue. */
  oio_req* req = oio_read_reqs_head(handle);
  if (!req) {
    ev_io_stop(EV_DEFAULT_ &(handle->read_watcher));
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

  oio_read_cb cb = req->cb;

  if (nread < 0) {
    if (errno == EAGAIN) {
      /* Just wait for the next one. */
      assert(ev_is_active(&(handle->read_watcher)));
    } else {
      oio_err err = oio_err_new(handle, errno);
      if (cb) {
        cb(req, 0);
      }
      handle->err = errno;
      oio_close(handle);
    }
  } else {
    /* Successful read */

    /* First pop the req off handle->read_reqs */
    ngx_queue_remove(&(req->read_reqs));

    free(req->read_bufs);
    req->read_bufs = NULL;

    /* NOTE: call callback AFTER freeing the request data. */
    if (cb) {
      cb(req, nread);
    }

    if (oio_read_reqs_empty(handle)) {
      ev_io_stop(EV_DEFAULT_ &(handle->read_watcher));
    }
  }
}


void oio_tcp_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;

  assert(handle->fd >= 0);

  if (oio_flag_is_set(handle, OIO_CLOSING)) {
    oio_finish_close(handle);
    return;
  }

  if (handle->connect_req) {
    oio_tcp_connect(handle, handle->connect_req);
  } else {
    if (revents & EV_READ) {
      oio__read(handle);
    }

    if (revents & EV_WRITE) {
      assert(0 && "Queued writes are not supported");
    }
  }
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
void oio_tcp_connect(oio_handle* handle, oio_req* req) {
  assert(handle->fd >= 0);
  assert(req);

  int error;
  int errorsize = sizeof(int);
  getsockopt(handle->fd, SOL_SOCKET, SO_ERROR, &error, &errorsize);

  if (!error) {
    ev_io_init(&handle->write_watcher, oio_tcp_io, handle->fd, EV_WRITE);
    ev_set_cb(&handle->read_watcher, oio_tcp_io);

    /* Successful connection */
    oio_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      connect_cb(req, oio_err_new(handle, 0));
    }

    req = NULL;

  } else if (error == EINPROGRESS) {
    /* Still connecting. */
    return;

  } else {
    oio_err err = oio_err_new(handle, error);

    if (req->connect_cb) {
      req->connect_cb(req, err);
    }

    handle->err = err;
    oio_close(handle);
  }
}


int oio_connect(oio_req *req, struct sockaddr* addr) {
  oio_handle* handle = req->handle;

  req->type = OIO_CONNECT;
  ngx_queue_init(&(req->read_reqs));

  if (handle->connect_req) {
    return oio_err_new(handle, EALREADY);
  }

  if (handle->type != OIO_TCP) {
    return oio_err_new(handle, ENOTSOCK);
  }

  handle->connect_req = req;

  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
    handle->fd = socket(AF_INET, SOCK_STREAM, 0);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
    handle->fd = socket(AF_INET6, SOCK_STREAM, 0);
  } else {
    assert(0);
    return -1;
  }

  /* socket(2) failed */
  if (handle->fd < 0) {
    return oio_err_new(handle, errno);
  }

  int r = connect(handle->fd, addr, addrsize);

  ev_io_init(&(handle->read_watcher), oio_tcp_io, handle->fd, EV_READ);
  ev_io_init(&(handle->write_watcher), oio_tcp_io, handle->fd, EV_WRITE);
  ev_io_start(EV_DEFAULT_ &(handle->read_watcher));

  return oio_err_new(handle, r);
}


int oio_write(oio_req *req, oio_buf* bufs, int bufcnt) {
  oio_handle* handle = req->handle;
  assert(handle->fd >= 0);
  ssize_t r;

  ngx_queue_init(&(req->read_reqs));
  req->type = OIO_WRITE;

  r = writev(handle->fd, (struct iovec*)bufs, bufcnt);

  if (r < 0) {
    return oio_err_new(handle, r);
  } else {
    if (req && req->cb) {
      oio_write_cb cb = req->cb;
      cb(req);
    }
    return 0;
  }
}


int oio_write2(oio_req* req, const char* msg) {
  size_t len = strnlen(msg, 1024 * 1024);
  oio_buf b;
  b.base = (char*)msg;
  b.len = len;
  return oio_write(req, &b, 1);
}


int oio_timeout(oio_req *req, int64_t timeout) {
  return -1;
}


int oio_read(oio_req *req, oio_buf* bufs, int bufcnt) {
  oio_handle* handle = req->handle;
  ssize_t nread = -1;
  errno = EAGAIN;
  oio_read_cb cb = req->cb;

  assert(handle->fd >= 0);

  if (ngx_queue_empty(&handle->read_reqs)) {
    /* Attempt to read immediately. */
    ssize_t nread = readv(handle->fd, (struct iovec*) bufs, bufcnt);
  }

  req->type = OIO_READ;
  ngx_queue_init(&(req->read_reqs));

  if (nread < 0 && errno != EAGAIN) {
    /* Real error. */
    oio_err err = oio_err_new(handle, errno);

    if (cb) {
      cb(req, nread);
    }

    return err;
  }

  if (nread >= 0) {
    /* Successful read. */
    if (cb) {
      cb(req, nread);
    }
    return 0;
  }

  /* Either we never read anything, or we got EAGAIN. */
  assert(!ngx_queue_empty(&handle->read_reqs) ||
         (nread < 0 && errno == EAGAIN));

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
   */
  req->read_bufs = malloc(sizeof(oio_buf) * bufcnt);
  memcpy(req->read_bufs, bufs, bufcnt * sizeof(oio_buf));
  req->read_bufcnt = bufcnt;

  /* Append the request to read_reqs. */
  ngx_queue_insert_tail(&(handle->read_reqs), &(req->read_reqs));

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
  ngx_queue_init(&(req->read_reqs));
}
