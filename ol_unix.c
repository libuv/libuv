#include "ol.h"


ol_loop* ol_loop_new() {
  ol_loop* loop = calloc(sizeof(ol_loop), 1);
  if (!loop) {
    return NULL;
  }

  loop.evloop = ev_loop_new(0);
  if (!loop.evloop) {
    return NULL;
  }

  return loop;
}


void ol_associate(ol_loop* loop, ol_handle* handle) {
  assert(!handle->loop);
  handle->loop = loop;
}


void ol_run(ol_loop *loop) {
  ev_run(loop, 0);
}


ol_handle* ol_tcp_new(int v4, ol_read_cb read_cb, ol_close_cb close_cb) {
  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  if (!handle) {
    return NULL;
  }

  handle->read_cb = read_cb;
  handle->close_cb = close_cb;

  handle->type = v4 ? OL_TCP : OL_TCP6;

  int domain = v4 ? AF_INET : AF_INET6;
  handle->fd = socket(domain, SOCK_STREAM, 0);
  if (fd == -1) {
    free(handle);
    got_error("socket", errno);
    return NULL;
  }

  return handle;
}


static void tcp_io(EV_P_ ev_io *w, int revents) {
  ol_handle* h = (ol_handle*)w->data;

  if (h->connecting) {
    tcp_check_connect_status(h);
  } else {

  }
}


static void tcp_check_connect_status(ol_handle* h) {
  assert(h->connecting);

  int error;
  socklen_t len = sizeof(int);
  getsockopt(h->fd, SOL_SOCKET, SO_ERROR, &error, &len);

  if (error == 0) {
    tcp_connected(h);
  } else if (errno != EINPROGRESS) {
    close(h->fd);
    got_error("connect", errno);
  }

  /* EINPROGRESS - unlikely. What to do? */
}


static void tcp_connected(ol_handle* h) {
  assert(h->connecting);
  if (h->connect_cb) {
    h->connect_cb(h);
  }
  h->connecting = 0;
  h->connect_cb = NULL;
}


int ol_connect(ol_handle* h, sockaddr* addr, sockaddr_len addrlen,
    ol_buf* buf, size_t* bytes_sent, ol_connect_cb cb) {
  if (h->connecting) {
    return got_error("connect", EALREADY);
  }

  h->connecting = 1;
  h->connect_addr = addr;
  h->connect_addrlen = addrlen;

  if (buf) {
    /* We're allowed to ol_write before the socket becomes connected. */
    ol_write(h, buf, 1, bytes_sent, cb);
  } else {
    h->connect_cb = cb;
  }

  int r = connect(h->fd, h->connect_addr, h->connect_addrlen);

  if (r != 0) {
    if (errno == EINPROGRESS) {
      /* Wait for fd to become writable. */
      h->connecting = 1;
      ev_io_init(&h->write_watcher, tcp_io, h->fd, EV_WRITE);
      ev_io_start(h->loop, &h->write_watcher);
    }
    return got_error("connect", errno);
  }

  /* Connected */
  tcp_connected(h);
  return 0;
}


int ol_get_fd(ol_handle* h) {
  return h->fd;
}
