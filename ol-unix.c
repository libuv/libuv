#include "ol.h"

#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <string.h> /* strnlen */
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


void ol_tcp_io(EV_P_ ev_io* watcher, int revents);
void ol_tcp_connect(ol_handle* handle, ol_req* req);
ol_handle* ol_tcp_open(ol_handle* parent, int fd);
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


ol_handle* ol_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  handle->close_cb = close_cb;
  handle->data = data;

  handle->_.fd = -1;

  ngx_queue_init(&handle->_.read_reqs);

  ev_init(&handle->_.read_watcher, ol_tcp_io);
  ev_init(&handle->_.write_watcher, ol_tcp_io);

  return handle;
}


int ol_tcp_lazy_open(ol_handle* handle, int domain) {
  assert(handle->_.fd < 0);

  /* Lazily allocate a file descriptor for this handle */
  int fd = socket(domain, SOCK_STREAM, 0);

  /* Set non-blocking, etc */
  ol_tcp_init_fd(fd);

  handle->_.fd = fd;

  return 0;
}


int ol_bind(ol_handle* handle, struct sockaddr* addr) {
  int addrsize;
  int domain;

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

  int r = 0;

  if (handle->_.fd < 0) {
    r = ol_tcp_lazy_open(handle, domain);
    if (r) {
      return ol_err_new(handle, r);
    }
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


ol_handle* ol_tcp_open(ol_handle* parent, int fd) {
  ol_handle* h = ol_handle_new(NULL, NULL);
  if (!h) {
    return NULL;
  }
  h->_.fd = fd;

  /* Set non-blocking, etc */
  ol_tcp_init_fd(fd);

  return h;
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
        ol_handle* new_client = ol_tcp_open(handle, fd);
        if (!new_client) {
          ol_close_error(handle, ol_err_last(handle));
          return;
        }

        handle->accept_cb(handle, new_client);
      }
    }
  }
}


int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb) {
  if (handle->_.fd < 0) {
    /* Lazily allocate a file descriptor for this handle */
    handle->_.fd = socket(AF_INET, SOCK_STREAM, 0);
  }

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


void ol_tcp_io(EV_P_ ev_io* watcher, int revents) {
  ol_handle* handle = watcher->data;

  if (handle->_.connect_req) {
    ol_tcp_connect(handle, handle->_.connect_req);
  } else {
  }

  assert(handle->_.fd >= 0);
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
void ol_tcp_connect(ol_handle* handle, ol_req* req) {
  assert(handle->_.fd >= 0);
  assert(req);
  assert(req->type == OL_CONNECT);

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
    in_req->handle = handle;
    in_req->_.local = 0;
    return in_req;
  } else {
    ol_req *req = calloc(sizeof(ol_req), 1);
    req->handle = handle;
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
  ssize_t r;

  r = writev(handle->_.fd, (struct iovec*)bufs, bufcnt);

  if (r < 0) {
    return ol_err_new(handle, r);
  } else {
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


void ol_req_append(ol_handle* handle, ol_req *req) {
  ngx_queue_insert_tail(&handle->_.read_reqs, &req->_.read_reqs);
}


int ol_read(ol_handle* handle, ol_req *req_in, ol_buf* bufs, int bufcnt) {
  assert(handle->_.fd >= 0);

  if (!ngx_queue_empty(&handle->_.read_reqs)) {
    /* There are already pending read_reqs. We must get in line. */
    assert(ev_is_active(&handle->_.read_watcher));

    ol_req* req = ol_req_maybe_alloc(handle, req_in);
    if (!req) {
      return ol_err_new(handle, ENOMEM);
    }

    ol_req_append(handle, req);

    return ol_err_new(handle, EINPROGRESS);

  } else {
    /* Attempt to read immediately */
    ssize_t nread = readv(handle->_.fd, (struct iovec*) bufs, bufcnt);

    ol_read_cb cb = req_in->cb;

    if (nread < 0) {
      if (errno == EAGAIN) {
        ev_io_start(EV_DEFAULT_ &handle->_.read_watcher);
        return 0;
      } else {
        ol_err err = ol_err_new(handle, errno);

        if (cb) {
          cb(req_in, nread, err);
        }

        return  err;
      }

    } else {
      if (cb) {
        cb(req_in, nread, 0);
      }
      return 0;
    }
  }

  assert(0 && "Unreachable");
  return 0;
}


void ol_free(ol_handle* handle) {
  free(handle);
  /* lists? */
  return;
}
