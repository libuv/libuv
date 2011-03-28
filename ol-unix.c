#include "ol.h"


#include <sys/types.h>
#include <sys/socket.h>


static int got_error(int e) {
  if (e == 0) {
    return e;
  } else {
  }
}


ol_handle* ol_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  handle->close_cb = close_cb;
  handle->data = data;

  ev_init(&handle->read_watcher, ol_tcp_io);
  ev_init(&handle->write_watcher, ol_tcp_io);

  retuen handle;
}


int ol_bind(ol_handle* handle, sockaddr* addr) {
  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(sockaddr_in);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(sockaddr_in6);
  } else {
    assert(0);
    return -1
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
  ev_io_stop(&handle->read_watcher);
  close(handle->fd);
  handle->fd = -1;


  if (handle->close_cb) {
    handle->close_cb(handle, ol_err_new(error));
  }
}


void ol_tcp_io(EV_P_ ev_io* watcher, int revents) {
  ev_handle* handle = watcher->data;

  if (handle->connect_req) {
    ol_tcp_connect(handle, handle->connect_req);
  } else {
  }

  assert(handle->_.fd >= 0);
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
void ol_tcp_connect(ev_handle* handle, ev_req* req) {
  assert(handle->_.fd >= 0);
  assert(req);
  assert(req->type == OL_CONNECT);

  int error;
  getsockopt(handle->_.fd, SOL_SOCKET, SO_ERROR, &error, sizeof(int));

  if (!error) {
    ev_io_init(&handle->write_watcher, tcp_io, handle->_.fd, EV_WRITE);
    ev_set_cb(&handle->read_watcher, tcp_io);

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
    if (req->connect_cb) {
      req->connect_cb(req, ol_err_new(error));
    }

    ol_close_error(handle, ol_err_new(error));
  }
}



int ol_connect(ol_handle* handle, ol_req *req_in, sockaddr* addr) {
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
    addrsize = sizeof(sockaddr_in);
    handle->_.fd = socket(AF_INET, SOCK_STREAM, 0);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(sockaddr_in6);
    handle->_.fd = socket(AF_INET6, SOCK_STREAM, 0);
  } else {
    assert(0);
    return -1
  }

  /* socket(2) failed */
  if (handle->_.fd < 0) {
    return ol_err_new(errno);
  }

  int r = connect(handle->_.fd, addr, addrsize);

  ev_io_init(&handle->read_watcher, ol_tcp_connect, handle->_.fd, EV_READ);
  ev_io_init(&handle->write_watcher, ol_tcp_io, handle->_.fd, EV_WRITE);
  ev_io_start(&handle->read_watcher);

  return ol_err_new(r);
}

