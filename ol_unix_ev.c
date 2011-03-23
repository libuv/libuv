


ol_loop* ol_loop_new()
{
  ol_loop* loop = malloc(sizeof(ol_loop));
  if (!loop) {
    return NULL;
  }

  loop.evloop = ev_loop_new(0);
  if (!loop.evloop) {
    return NULL;
  }


  return loop;
}


ol_loop* ol_associate(ol_handle* handle)
{
}


void ol_run(ol_loop *loop) {
  ev_run(loop, 0);
}


ol_handle* ol_tcp_new(int v4, ol_read_cb read_cb, ol_close_cb close_cb) {
  ol_handle *handle = malloc(sizeof(ol_handle));
  if (!handle) {
    return NULL;
  }

  handle->read_cb = read_cb;
  handle->close_cb = close_cb;

  int domain = v4 ? AF_INET : AF_INET6;
  handle->fd = socket(domain, SOCK_STREAM, 0);
  if (fd == -1) {
    free(handle);
    got_error("socket", errno);
    return NULL;
  }

  return handle;
}


void handle_tcp_io() {

}


int try_connect(ol_handle* h) {
  int r = connect(h->fd, h->connect_addr, h->connect_addrlen);

  if (r != 0) {
    if (errno == EINPROGRESS) {
      /* Wait for fd to become writable. */
      h->connecting = 1;
      ev_io_init(&h->write_watcher, handle_tcp_io, h->fd, EV_WRITE);
      ev_io_start(h->loop, &h->write_watcher);
    }
    return got_error("connect", errno);
  }

  /* Connected */
  if (h->connect_cb) {
    h->connect_cb(h);
    h->connecting = 0;
    h->connect_cb = NULL;
  }

  return 0;
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
    ol_write(h, buf, 1, bytes_sent, cb);
  } else {
    h->connect_cb = cb;
  }

  return try_connect(h);
}


int ol_get_fd(ol_handle* h) {
  return h->fd;
}
