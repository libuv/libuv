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
int oio_close_error(oio_handle* handle, oio_err err);


static oio_err oio_err_new(oio_handle* handle, int e) {
  handle->_.err = e;
  return e;
}

oio_err oio_err_last(oio_handle *handle) {
  return handle->_.err;
}


struct sockaddr_in oio_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int oio_close(oio_handle* handle) {
  return oio_close_error(handle, 0);
}


void oio_init() {
  ev_default_loop(0);
}


int oio_run() {
  ev_run(EV_DEFAULT_ 0);
}


oio_handle* oio_tcp_handle_new(oio_close_cb close_cb, void* data) {
  oio_handle *handle = calloc(sizeof(oio_handle), 1);
  if (!handle) {
    oio_err_new(NULL, ENOMEM);
    return NULL;
  }

  handle->type = OIO_TCP;
  handle->close_cb = close_cb;
  handle->data = data;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    oio_err_new(handle, errno);
    free(handle);
    return NULL;
  }

  if (oio_tcp_open(handle, fd)) {
    close(fd);
    free(handle);
    return NULL;
  }

  return handle;
}



int oio_bind(oio_handle* handle, struct sockaddr* addr) {
  int addrsize;
  int domain;
  int r;

  assert(handle->_.fd >= 0);

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

  r = bind(handle->_.fd, addr, addrsize);

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

  handle->_.fd = fd;

  ngx_queue_init(&handle->_.read_reqs);

  ev_io_init(&handle->_.read_watcher, oio_tcp_io, fd, EV_READ);
  ev_io_init(&handle->_.write_watcher, oio_tcp_io, fd, EV_WRITE);

  handle->_.read_watcher.data = handle;
  handle->_.write_watcher.data = handle;

  return 0;
}


void oio_server_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;

  assert(revents == EV_READ);

  while (1) {
    struct sockaddr addr;
    socklen_t addrlen;
    int fd = accept(handle->_.fd, &addr, &addrlen);

    if (fd < 0) {
      if (errno == EAGAIN) {
        return; /* No problem. */
      } else if (errno == EMFILE) {
        /* TODO special trick. unlock reserved socket, accept, close. */
        return;
      } else {
        oio_close_error(handle, oio_err_new(handle, errno));
      }

    } else {
      if (!handle->accept_cb) {
        close(fd);
      } else {
        oio_handle* new_client = oio_tcp_handle_new(NULL, NULL);
        if (!new_client) {
          /* Ignore error for now */
        } else {
          if (oio_tcp_open(new_client, fd)) {
            /* Ignore error for now */
          } else {
            ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);
            handle->accept_cb(handle, new_client);
          }
        }
      }
    }
  }
}


int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb) {
  assert(handle->_.fd >= 0);

  int r = listen(handle->_.fd, backlog);
  if (r < 0) {
    return oio_err_new(handle, errno);
  }

  handle->accept_cb = cb;
  ev_io_init(&handle->_.read_watcher, oio_server_io, handle->_.fd, EV_READ);
  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);
  handle->_.read_watcher.data = handle;

  return 0;
}


int oio_close_error(oio_handle* handle, oio_err err) {
  ev_io_stop(EV_DEFAULT_ &handle->_.read_watcher);
  close(handle->_.fd);
  handle->_.fd = -1;

  if (handle->close_cb) {
    handle->close_cb(handle, err);
  }

  return err;
}


oio_req* oio_read_reqs_head(oio_handle* handle) {
  ngx_queue_t* q = ngx_queue_head(&(handle->_.read_reqs));
  if (!q) {
    return NULL;
  }

  oio_req_private* p = ngx_queue_data(q, oio_req_private, read_reqs);
  assert(p);
  int off =  offsetof(oio_req, _);
  oio_req* req = (oio_req*)  ((char*)p - off);

  return req;
}


int oio_read_reqs_empty(oio_handle* handle) {
  return ngx_queue_empty(&(handle->_.read_reqs));
}


void oio__read(oio_handle* handle) {
  assert(handle->_.fd >= 0);

  /* Get the request at the head of the read_reqs queue. */
  oio_req* req = oio_read_reqs_head(handle);
  if (!req) {
    ev_io_stop(EV_DEFAULT_ &(handle->_.read_watcher));
    return;
  }

  /* Cast to iovec. We had to have our own oio_buf instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  struct iovec* iov = (struct iovec*) req->_.read_bufs;
  int iovcnt = req->_.read_bufcnt;

  assert(iov);
  assert(iovcnt > 0);

  /* Now do the actual read. */

  ssize_t nread = readv(handle->_.fd, iov, iovcnt);

  oio_read_cb cb = req->cb;

  if (nread < 0) {
    if (errno == EAGAIN) {
      /* Just wait for the next one. */
      assert(ev_is_active(&(handle->_.read_watcher)));
    } else {
      oio_err err = oio_err_new(handle, errno);
      if (cb) {
        cb(req, 0);
      }
      oio_close_error(handle, errno);
    }
  } else {
    /* Successful read */

    /* First pop the req off handle->_.read_reqs */
    ngx_queue_remove(&(req->_.read_reqs));

    /* Must free req if local. Also must free req->_.read_bufs. */
    free(req->_.read_bufs);
    req->_.read_bufs = NULL;
    if (req->_.local) {
      free(req);
    }

    /* NOTE: call callback AFTER freeing the request data. */
    if (cb) {
      cb(req, nread);
    }

    if (oio_read_reqs_empty(handle)) {
      ev_io_stop(EV_DEFAULT_ &(handle->_.read_watcher));
    }
  }
}


void oio_tcp_io(EV_P_ ev_io* watcher, int revents) {
  oio_handle* handle = watcher->data;

  assert(handle->_.fd >= 0);

  if (handle->_.connect_req) {
    oio_tcp_connect(handle, handle->_.connect_req);
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
  assert(handle->_.fd >= 0);
  assert(req);

  int error;
  int errorsize = sizeof(int);
  getsockopt(handle->_.fd, SOL_SOCKET, SO_ERROR, &error, &errorsize);

  if (!error) {
    ev_io_init(&handle->_.write_watcher, oio_tcp_io, handle->_.fd, EV_WRITE);
    ev_set_cb(&handle->_.read_watcher, oio_tcp_io);

    /* Successful connection */
    oio_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      if (req->_.local) {
        connect_cb(NULL, oio_err_new(handle, 0));
      } else {
        connect_cb(req, oio_err_new(handle, 0));
      }
    }

    /* Free up connect_req if we own it. */
    if (req->_.local) {
      free(req);
    }

    req = NULL;

  } else if (error == EINPROGRESS) {
    /* Still connecting. */
    return;

  } else {
    oio_err err = oio_err_new(handle, error);

    if (req->_.connect_cb) {
      req->_.connect_cb(req, err);
    }

    oio_close_error(handle, err);
  }
}


oio_req* oio_req_maybe_alloc(oio_handle* handle, oio_req* in_req) {
  if (in_req) {
    ngx_queue_init(&(in_req->_.read_reqs));
    in_req->handle = handle;
    in_req->_.local = 0;
    return in_req;
  } else {
    oio_req *req = malloc(sizeof(oio_req));
    oio_req_init(req, NULL);
    req->handle = handle;
    ngx_queue_init(&(req->_.read_reqs));
    req->_.local = 1;
    return req;
  }
}


int oio_connect(oio_handle* handle, oio_req *req_in, struct sockaddr* addr) {
  if (handle->_.connect_req) {
    return oio_err_new(handle, EALREADY);
  }

  if (handle->type != OIO_TCP) {
    return oio_err_new(handle, ENOTSOCK);
  }

  oio_req *req = oio_req_maybe_alloc(handle, req_in);
  if (!req) {
    return oio_err_new(handle, ENOMEM);
  }

  handle->_.connect_req = req;


  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
    handle->_.fd = socket(AF_INET, SOCK_STREAM, 0);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
    handle->_.fd = socket(AF_INET6, SOCK_STREAM, 0);
  } else {
    assert(0);
    return -1;
  }

  /* socket(2) failed */
  if (handle->_.fd < 0) {
    return oio_err_new(handle, errno);
  }

  int r = connect(handle->_.fd, addr, addrsize);

  ev_io_init(&handle->_.read_watcher, oio_tcp_io, handle->_.fd, EV_READ);
  ev_io_init(&handle->_.write_watcher, oio_tcp_io, handle->_.fd, EV_WRITE);
  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);

  return oio_err_new(handle, r);
}


int oio_write(oio_handle* handle, oio_req *req, oio_buf* bufs, int bufcnt) {
  assert(handle->_.fd >= 0);
  ssize_t r;

  r = writev(handle->_.fd, (struct iovec*)bufs, bufcnt);

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


int oio_write2(oio_handle* handle, const char* msg) {
  size_t len = strnlen(msg, 1024 * 1024);
  oio_buf b;
  b.base = (char*)msg;
  b.len = len;
  return oio_write(handle, NULL, &b, 1);
}


int oio_read(oio_handle* handle, oio_req *req_in, oio_buf* bufs, int bufcnt) {
  ssize_t nread = -1;
  errno = EAGAIN;
  oio_read_cb cb = req_in->cb;

  assert(handle->_.fd >= 0);

  if (ngx_queue_empty(&handle->_.read_reqs)) {
    /* Attempt to read immediately. */
    ssize_t nread = readv(handle->_.fd, (struct iovec*) bufs, bufcnt);
  }

  if (nread < 0 && errno != EAGAIN) {
    /* Real error. */
    oio_err err = oio_err_new(handle, errno);

    if (cb) {
      cb(req_in, nread);
    }

    return err;
  }

  if (nread >= 0) {
    /* Successful read. */
    if (cb) {
      cb(req_in, nread);
    }
    return 0;
  }

  /* Either we never read anything, or we got EAGAIN. */
  assert(!ngx_queue_empty(&handle->_.read_reqs) ||
         (nread < 0 && errno == EAGAIN));

  /* Two possible states:
   * - EAGAIN, meaning the socket is not wriable currently. We must wait for
   *   it to become readable with the handle->_.read_watcher.
   * - The read_reqs queue already has reads. Meaning: the user has issued
   *   many oio_reads calls some of which are still waiting for the socket to
   *   become readable.
   * In the meantime we append the request to handle->_.read_reqs
   */
  oio_req* req = oio_req_maybe_alloc(handle, req_in);
  if (!req) {
    return oio_err_new(handle, ENOMEM);
  }

  /* Copy the bufs data over into our oio_req struct. This is so the user can
   * free the oio_buf array. The actual data inside the oio_bufs is however
   * owned by the user and cannot be deallocated until the read completes.
   */
  req->_.read_bufs = malloc(sizeof(oio_buf) * bufcnt);
  memcpy(req->_.read_bufs, bufs, bufcnt * sizeof(oio_buf));
  req->_.read_bufcnt = bufcnt;

  /* Append the request to read_reqs. */
  ngx_queue_insert_tail(&(handle->_.read_reqs), &(req->_.read_reqs));

  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);

  return oio_err_new(handle, EINPROGRESS);
}


void oio_free(oio_handle* handle) {
  free(handle);
  /* lists? */
  return;
}


void oio_req_init(oio_req *req, void *cb) {
  req->type = OIO_UNKNOWN_REQ;
  req->cb = cb;
  ngx_queue_init(&(req->_.read_reqs));
}
