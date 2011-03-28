#include "ol.h"

#include <stdlib.h>
#include <errno.h>
#include <assert.h>


void ol_tcp_io(EV_P_ ev_io* watcher, int revents);
void ol_tcp_connect(ol_handle* handle, ol_req* req);


static int ol_err_new(int e) {
  if (e == 0) {
    return e;
  } else {
  }
}


struct sockaddr_in ol_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


ol_handle* ol_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  handle->close_cb = close_cb;
  handle->data = data;

  ev_init(&handle->_.read_watcher, ol_tcp_io);
  ev_init(&handle->_.write_watcher, ol_tcp_io);

  return handle;
}


int ol_bind(ol_handle* handle, struct sockaddr* addr) {
  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
  } else {
    assert(0);
    return -1;
  }

  int r = bind(handle->_.fd, addr, addrsize);

  return ol_err_new(r);
}


int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb) {
  int r = listen(handle->_.fd, backlog);
  handle->accept_cb = cb;
  return ol_err_new(r);
}


void ol_close_error(ol_handle* handle, ol_err err) {
  ev_io_stop(&handle->_.read_watcher);
  close(handle->_.fd);
  handle->_.fd = -1;


  if (handle->close_cb) {
    handle->close_cb(handle, err);
  }
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
    ol_connect_cb connect_cb = req->connect_cb;
    if (connect_cb) {
      if (req->_.local) {
        connect_cb(NULL, ol_err_new(0));
      } else {
        connect_cb(req, ol_err_new(0));
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
    if (req->_.connect_cb) {
      req->_.connect_cb(req, ol_err_new(error));
    }

    ol_close_error(handle, ol_err_new(error));
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
    return ol_err_new(EALREADY);
  }

  if (handle->type != OL_TCP) {
    return ol_err_new(ENOTSOCK);
  }

  ol_req *req = ol_req_maybe_alloc(handle, req_in);
  if (!req) {
    return ol_err_new(ENOMEM);
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
    return ol_err_new(errno);
  }

  int r = connect(handle->_.fd, addr, addrsize);

  ev_io_init(&handle->_.read_watcher, ol_tcp_io, handle->_.fd, EV_READ);
  ev_io_init(&handle->_.write_watcher, ol_tcp_io, handle->_.fd, EV_WRITE);
  ev_io_start(&handle->_.read_watcher);

  return ol_err_new(r);
}

