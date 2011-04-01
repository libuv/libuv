#include "ol.h"

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


void ol_tcp_io(EV_P_ ev_io* watcher, int revents);
void ol_tcp_connect(ol_handle* handle, ol_req* req);
int ol_tcp_open(ol_handle*, int fd);
int ol_close_error(ol_handle* handle, ol_err err);


static ol_err ol_err_new(ol_handle* handle, int e) {
  handle->_.err = e;
  return e;
}

ol_err ol_err_last(ol_handle *handle) {
  return handle->_.err;
}


struct sockaddr_in ol_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int ol_close(ol_handle* handle) {
  return ol_close_error(handle, 0);
}


void ol_init() {
  ev_default_loop(0);
}


int ol_run() {
  ev_run(EV_DEFAULT_ 0);
}


ol_handle* ol_tcp_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  if (!handle) {
    ol_err_new(NULL, ENOMEM);
    return NULL;
  }

  handle->type = OL_TCP;
  handle->close_cb = close_cb;
  handle->data = data;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    ol_err_new(handle, errno);
    free(handle);
    return NULL;
  }

  if (ol_tcp_open(handle, fd)) {
    close(fd);
    free(handle);
    return NULL;
  }

  return handle;
}



int ol_bind(ol_handle* handle, struct sockaddr* addr) {
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

  return ol_err_new(handle, r);
}


int ol_tcp_init_fd(int fd) {
  int r;
  int yes = 1;
  r = fcntl(fd, F_SETFL, O_NONBLOCK);
  assert(r == 0);
  r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(r == 0);
  return 0;
}


int ol_tcp_open(ol_handle* handle, int fd) {
  /* Set non-blocking, etc */
  ol_tcp_init_fd(fd);

  handle->_.fd = fd;

  ngx_queue_init(&handle->_.read_reqs);

  ev_io_init(&handle->_.read_watcher, ol_tcp_io, fd, EV_READ);
  ev_io_init(&handle->_.write_watcher, ol_tcp_io, fd, EV_WRITE);

  handle->_.read_watcher.data = handle;
  handle->_.write_watcher.data = handle;

  return 0;
}


void ol_server_io(EV_P_ ev_io* watcher, int revents) {
  ol_handle* handle = watcher->data;

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
        ol_close_error(handle, ol_err_new(handle, errno));
      }

    } else {
      if (!handle->accept_cb) {
        close(fd);
      } else {
        ol_handle* new_client = ol_tcp_handle_new(NULL, NULL);
        if (!new_client) {
          /* Ignore error for now */
        } else {
          if (ol_tcp_open(new_client, fd)) {
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


int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb) {
  assert(handle->_.fd >= 0);

  int r = listen(handle->_.fd, backlog);
  if (r < 0) {
    return ol_err_new(handle, errno);
  }

  handle->accept_cb = cb;
  ev_io_init(&handle->_.read_watcher, ol_server_io, handle->_.fd, EV_READ);
  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);
  handle->_.read_watcher.data = handle;

  return 0;
}


int ol_close_error(ol_handle* handle, ol_err err) {
  ev_io_stop(EV_DEFAULT_ &handle->_.read_watcher);
  close(handle->_.fd);
  handle->_.fd = -1;

  if (handle->close_cb) {
    handle->close_cb(handle, err);
  }

  return err;
}


ol_req* ol_read_reqs_head(ol_handle* handle) {
  ngx_queue_t* q = ngx_queue_head(&(handle->_.read_reqs));
  if (!q) {
    return NULL;
  }

  ol_req_private* p = ngx_queue_data(q, ol_req_private, read_reqs);
  assert(p);
  int off =  offsetof(ol_req, _);
  ol_req* req = (ol_req*)  ((char*)p - off);

  return req;
}


int ol_read_reqs_empty(ol_handle* handle) {
  return ngx_queue_empty(&(handle->_.read_reqs));
}


void ol__read(ol_handle* handle) {
  assert(handle->_.fd >= 0);

  /* Get the request at the head of the read_reqs queue. */
  ol_req* req = ol_read_reqs_head(handle);
  if (!req) {
    ev_io_stop(EV_DEFAULT_ &(handle->_.read_watcher));
    return;
  }

  /* Cast to iovec. We had to have our own ol_buf instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  struct iovec* iov = (struct iovec*) req->_.read_bufs;
  int iovcnt = req->_.read_bufcnt;

  assert(iov);
  assert(iovcnt > 0);

  /* Now do the actual read. */

  ssize_t nread = readv(handle->_.fd, iov, iovcnt);

  ol_read_cb cb = req->cb;

  if (nread < 0) {
    if (errno == EAGAIN) {
      /* Just wait for the next one. */
      assert(ev_is_active(&(handle->_.read_watcher)));
    } else {
      ol_err err = ol_err_new(handle, errno);
      if (cb) {
        cb(req, 0);
      }
      ol_close_error(handle, errno);
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

    if (ol_read_reqs_empty(handle)) {
      ev_io_stop(EV_DEFAULT_ &(handle->_.read_watcher));
    }
  }
}


void ol_tcp_io(EV_P_ ev_io* watcher, int revents) {
  ol_handle* handle = watcher->data;

  assert(handle->_.fd >= 0);

  if (handle->_.connect_req) {
    ol_tcp_connect(handle, handle->_.connect_req);
  } else {
    if (revents & EV_READ) {
      ol__read(handle);
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
void ol_tcp_connect(ol_handle* handle, ol_req* req) {
  assert(handle->_.fd >= 0);
  assert(req);

  int error;
  int errorsize = sizeof(int);
  getsockopt(handle->_.fd, SOL_SOCKET, SO_ERROR, &error, &errorsize);

  if (!error) {
    ev_io_init(&handle->_.write_watcher, ol_tcp_io, handle->_.fd, EV_WRITE);
    ev_set_cb(&handle->_.read_watcher, ol_tcp_io);

    /* Successful connection */
    ol_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      if (req->_.local) {
        connect_cb(NULL, ol_err_new(handle, 0));
      } else {
        connect_cb(req, ol_err_new(handle, 0));
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
    ol_err err = ol_err_new(handle, error);

    if (req->_.connect_cb) {
      req->_.connect_cb(req, err);
    }

    ol_close_error(handle, err);
  }
}


ol_req* ol_req_maybe_alloc(ol_handle* handle, ol_req* in_req) {
  if (in_req) {
    ngx_queue_init(&(in_req->_.read_reqs));
    in_req->handle = handle;
    in_req->_.local = 0;
    return in_req;
  } else {
    ol_req *req = malloc(sizeof(ol_req));
    ol_req_init(req, NULL);
    req->handle = handle;
    ngx_queue_init(&(req->_.read_reqs));
    req->_.local = 1;
    return req;
  }
}


int ol_connect(ol_handle* handle, ol_req *req_in, struct sockaddr* addr) {
  if (handle->_.connect_req) {
    return ol_err_new(handle, EALREADY);
  }

  if (handle->type != OL_TCP) {
    return ol_err_new(handle, ENOTSOCK);
  }

  ol_req *req = ol_req_maybe_alloc(handle, req_in);
  if (!req) {
    return ol_err_new(handle, ENOMEM);
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
    return ol_err_new(handle, errno);
  }

  int r = connect(handle->_.fd, addr, addrsize);

  ev_io_init(&handle->_.read_watcher, ol_tcp_io, handle->_.fd, EV_READ);
  ev_io_init(&handle->_.write_watcher, ol_tcp_io, handle->_.fd, EV_WRITE);
  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);

  return ol_err_new(handle, r);
}


int ol_write(ol_handle* handle, ol_req *req, ol_buf* bufs, int bufcnt) {
  assert(handle->_.fd >= 0);
  ssize_t r;

  r = writev(handle->_.fd, (struct iovec*)bufs, bufcnt);

  if (r < 0) {
    return ol_err_new(handle, r);
  } else {
    if (req && req->cb) {
      ol_write_cb cb = req->cb;
      cb(req);
    }
    return 0;
  }
}


int ol_write2(ol_handle* handle, const char* msg) {
  size_t len = strnlen(msg, 1024 * 1024);
  ol_buf b;
  b.base = (char*)msg;
  b.len = len;
  return ol_write(handle, NULL, &b, 1);
}


int ol_read(ol_handle* handle, ol_req *req_in, ol_buf* bufs, int bufcnt) {
  ssize_t nread = -1;
  errno = EAGAIN;
  ol_read_cb cb = req_in->cb;

  assert(handle->_.fd >= 0);

  if (ngx_queue_empty(&handle->_.read_reqs)) {
    /* Attempt to read immediately. */
    ssize_t nread = readv(handle->_.fd, (struct iovec*) bufs, bufcnt);
  }

  if (nread < 0 && errno != EAGAIN) {
    /* Real error. */
    ol_err err = ol_err_new(handle, errno);

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
   *   many ol_reads calls some of which are still waiting for the socket to
   *   become readable.
   * In the meantime we append the request to handle->_.read_reqs
   */
  ol_req* req = ol_req_maybe_alloc(handle, req_in);
  if (!req) {
    return ol_err_new(handle, ENOMEM);
  }

  /* Copy the bufs data over into our ol_req struct. This is so the user can
   * free the ol_buf array. The actual data inside the ol_bufs is however
   * owned by the user and cannot be deallocated until the read completes.
   */
  req->_.read_bufs = malloc(sizeof(ol_buf) * bufcnt);
  memcpy(req->_.read_bufs, bufs, bufcnt * sizeof(ol_buf));
  req->_.read_bufcnt = bufcnt;

  /* Append the request to read_reqs. */
  ngx_queue_insert_tail(&(handle->_.read_reqs), &(req->_.read_reqs));

  ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);

  return ol_err_new(handle, EINPROGRESS);
}


void ol_free(ol_handle* handle) {
  free(handle);
  /* lists? */
  return;
}


void ol_req_init(ol_req *req, void *cb) {
  req->type = OL_UNKNOWN_REQ;
  req->cb = cb;
  ngx_queue_init(&(req->_.read_reqs));
}
