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

#include <stddef.h> /* NULL */
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
static oio_alloc_cb alloc_cb;


void oio__tcp_io(EV_P_ ev_io* watcher, int revents);
void oio__next(EV_P_ ev_idle* watcher, int revents);
static void oio_tcp_connect(oio_handle* handle);
int oio_tcp_open(oio_handle*, int fd);
void oio_finish_close(oio_handle* handle);


/* flags */
enum {
  OIO_CLOSING  = 0x00000001, /* oio_close() called but not finished. */
  OIO_CLOSED   = 0x00000002, /* close(2) finished. */
  OIO_READING  = 0x00000004, /* oio_read_start() called. */
  OIO_SHUTTING = 0x00000008, /* oio_shutdown() called but not complete. */
  OIO_SHUT     = 0x00000010, /* Write side closed. */
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
    case EACCES: return OIO_EACCESS;
    case EAGAIN: return OIO_EAGAIN;
    case ECONNRESET: return OIO_ECONNRESET;
    case EFAULT: return OIO_EFAULT;
    case EMFILE: return OIO_EMFILE;
    case EINVAL: return OIO_EINVAL;
    case ECONNREFUSED: return OIO_ECONNREFUSED;
    case EADDRINUSE: return OIO_EADDRINUSE;
    case EADDRNOTAVAIL: return OIO_EADDRNOTAVAIL;
    default: return OIO_UNKNOWN;
  }
}


static oio_err oio_err_new_artificial(oio_handle* handle, int code) {
  oio_err err;
  err.sys_errno_ = 0;
  err.code = code;
  last_err = err;
  return err;
}


static oio_err oio_err_new(oio_handle* handle, int sys_error) {
  oio_err err;
  err.sys_errno_ = sys_error;
  err.code = oio_translate_sys_error(sys_error);
  last_err = err;
  return err;
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


void oio_init(oio_alloc_cb cb) {
  assert(cb);
  alloc_cb = cb;
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

  ngx_queue_init(&handle->write_queue);
  handle->write_queue_size = 0;

  ev_init(&handle->next_watcher, oio__next);
  handle->next_watcher.data = handle;

  ev_init(&handle->read_watcher, oio__tcp_io);
  handle->read_watcher.data = handle;

  ev_init(&handle->write_watcher, oio__tcp_io);
  handle->write_watcher.data = handle;

  assert(ngx_queue_empty(&handle->write_queue));
  assert(handle->write_queue_size == 0);

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
    oio_err_new(handle, EFAULT);
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

  /* Associate the fd with each ev_io watcher. */
  ev_io_set(&handle->read_watcher, fd, EV_READ);
  ev_io_set(&handle->write_watcher, fd, EV_WRITE);

  /* These should have been set up by oio_tcp_init. */
  assert(handle->next_watcher.data == handle);
  assert(handle->write_watcher.data == handle);
  assert(handle->read_watcher.data == handle);
  assert(handle->read_watcher.cb == oio__tcp_io);
  assert(handle->write_watcher.cb == oio__tcp_io);

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
        oio_err_new(handle, errno);
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


oio_req* oio_write_queue_head(oio_handle* handle) {
  if (ngx_queue_empty(&handle->write_queue)) {
    return NULL;
  }

  ngx_queue_t* q = ngx_queue_head(&handle->write_queue);
  if (!q) {
    return NULL;
  }

  oio_req* req = ngx_queue_data(q, struct oio_req_s, queue);
  assert(req);

  return req;
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


static void oio__drain(oio_handle* handle) {
  assert(!oio_write_queue_head(handle));
  assert(handle->write_queue_size == 0);

  ev_io_stop(EV_DEFAULT_ &handle->write_watcher);

  /* Shutdown? */
  if (oio_flag_is_set(handle, OIO_SHUTTING) &&
      !oio_flag_is_set(handle, OIO_CLOSING) &&
      !oio_flag_is_set(handle, OIO_SHUT)) {
    assert(handle->shutdown_req);

    oio_req* req = handle->shutdown_req;
    oio_shutdown_cb cb = req->cb;

    if (shutdown(handle->fd, SHUT_WR)) {
      /* Error. Nothing we can do, close the handle. */
      oio_err_new(handle, errno);
      oio_close(handle);
      if (cb) cb(req, -1);
    } else {
      oio_err_new(handle, 0);
      oio_flag_set(handle, OIO_SHUT);
      if (cb) cb(req, 0);
    }
  }
}


void oio__write(oio_handle* handle) {
  assert(handle->fd >= 0);

  /* TODO: should probably while(1) here until EAGAIN */

  /* Get the request at the head of the queue. */
  oio_req* req = oio_write_queue_head(handle);
  if (!req) {
    assert(handle->write_queue_size == 0);
    oio__drain(handle);
    return;
  }

  assert(req->handle == handle);

  /* Cast to iovec. We had to have our own oio_buf instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  assert(sizeof(oio_buf) == sizeof(struct iovec));
  struct iovec* iov = (struct iovec*) &(req->bufs[req->write_index]);
  int iovcnt = req->bufcnt - req->write_index;

  /* Now do the actual writev. Note that we've been updating the pointers
   * inside the iov each time we write. So there is no need to offset it.
   */

  ssize_t n = writev(handle->fd, iov, iovcnt);

  oio_write_cb cb = req->cb;

  if (n < 0) {
    if (errno != EAGAIN) {
      oio_err err = oio_err_new(handle, errno);

      /* XXX How do we handle the error? Need test coverage here. */
      oio_close(handle);

      if (cb) {
        cb(req, -1);
      }
      return;
    }
  } else {
    /* Successful write */

    /* The loop updates the counters. */
    while (n > 0) {
      oio_buf* buf = &(req->bufs[req->write_index]);
      size_t len = buf->len;

      assert(req->write_index < req->bufcnt);

      if (n < len) {
        buf->base += n;
        buf->len -= n;
        handle->write_queue_size -= n;
        n = 0;

        /* There is more to write. Break and ensure the watcher is pending. */
        break;

      } else {
        /* Finished writing the buf at index req->write_index. */
        req->write_index++;

        assert(n >= len);
        n -= len;

        assert(handle->write_queue_size >= len);
        handle->write_queue_size -= len;

        if (req->write_index == req->bufcnt) {
          /* Then we're done! */
          assert(n == 0);

          /* Pop the req off handle->write_queue. */
          ngx_queue_remove(&req->queue);
          free(req->bufs); /* FIXME: we should not be allocing for each read */
          req->bufs = NULL;

          /* NOTE: call callback AFTER freeing the request data. */
          if (cb) {
            cb(req, 0);
          }

          if (!ngx_queue_empty(&handle->write_queue)) {
            assert(handle->write_queue_size > 0);
          } else {
            /* Write queue drained. */
            oio__drain(handle);
          }

          return;
        }
      }
    }
  }

  /* Either we've counted n down to zero or we've got EAGAIN. */
  assert(n == 0 || n == -1);

  /* We're not done yet. */
  assert(ev_is_active(&handle->write_watcher));
  ev_io_start(EV_DEFAULT_ &handle->write_watcher);
}


void oio__read(oio_handle* handle) {
  /* XXX: Maybe instead of having OIO_READING we just test if
   * handle->read_cb is NULL or not?
   */
  while (handle->read_cb && oio_flag_is_set(handle, OIO_READING)) {
    assert(alloc_cb);
    oio_buf buf = alloc_cb(handle, 64 * 1024);

    assert(buf.len > 0);
    assert(buf.base);

    struct iovec* iov = (struct iovec*) &buf;

    ssize_t nread = readv(handle->fd, iov, 1);

    if (nread < 0) {
      /* Error */
      if (errno == EAGAIN) {
        /* Wait for the next one. */
        if (oio_flag_is_set(handle, OIO_READING)) {
          ev_io_start(EV_DEFAULT_UC_ &handle->read_watcher);
        }
        oio_err_new(handle, EAGAIN);
        handle->read_cb(handle, 0, buf);
        return;
      } else {
        oio_err_new(handle, errno);
        oio_close(handle);
        handle->read_cb(handle, -1, buf);
        assert(!ev_is_active(&handle->read_watcher));
        return;
      }
    } else if (nread == 0) {
      /* EOF */
      oio_err_new_artificial(handle, OIO_EOF);
      ev_io_stop(EV_DEFAULT_UC_ &handle->read_watcher);
      handle->read_cb(handle, -1, buf);

      if (oio_flag_is_set(handle, OIO_SHUT)) {
        oio_close(handle);
      }
      return;
    } else {
      /* Successful read */
      handle->read_cb(handle, nread, buf);
    }
  }
}


int oio_shutdown(oio_req* req) {
  oio_handle* handle = req->handle;
  assert(handle->fd >= 0);

  if (oio_flag_is_set(handle, OIO_SHUT) ||
      oio_flag_is_set(handle, OIO_CLOSED) ||
      oio_flag_is_set(handle, OIO_CLOSING)) {
    return -1;
  }

  handle->shutdown_req = req;
  req->type = OIO_SHUTDOWN;

  oio_flag_set(handle, OIO_SHUTTING);

  ev_io_start(EV_DEFAULT_UC_ &handle->write_watcher);

  return 0;
}


void oio__tcp_io(EV_P_ ev_io* watcher, int revents) {
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
      oio__write(handle);
    }
  }
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
static void oio_tcp_connect(oio_handle* handle) {
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

    handle->connect_req = NULL;

    oio_connect_cb connect_cb = req->cb;
    if (connect_cb) {
      connect_cb(req, -1);
    }

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
  ngx_queue_init(&req->queue);

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


static size_t oio__buf_count(oio_buf bufs[], int bufcnt) {
  size_t total = 0;
  int i;

  for (i = 0; i < bufcnt; i++) {
    total += bufs[i].len;
  }

  return total;
}


/* The buffers to be written must remain valid until the callback is called.
 * This is not required for the oio_buf array.
 */
int oio_write(oio_req* req, oio_buf* bufs, int bufcnt) {
  oio_handle* handle = req->handle;
  assert(handle->fd >= 0);

  ngx_queue_init(&req->queue);
  req->type = OIO_WRITE;

  req->bufs = malloc(sizeof(oio_buf) * bufcnt);
  memcpy(req->bufs, bufs, bufcnt * sizeof(oio_buf));
  req->bufcnt = bufcnt;

  req->write_index = 0;
  handle->write_queue_size += oio__buf_count(bufs, bufcnt);

  /* Append the request to write_queue. */
  ngx_queue_insert_tail(&handle->write_queue, &req->queue);

  assert(!ngx_queue_empty(&handle->write_queue));
  assert(handle->write_watcher.cb == oio__tcp_io);
  assert(handle->write_watcher.data == handle);
  assert(handle->write_watcher.fd == handle->fd);

  ev_io_start(EV_DEFAULT_ &handle->write_watcher);

  return 0;
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


int oio_read_start(oio_handle* handle, oio_read_cb cb) {
  /* The OIO_READING flag is irrelevant of the state of the handle - it just
   * expresses the desired state of the user.
   */
  oio_flag_set(handle, OIO_READING);

  /* TODO: try to do the read inline? */
  /* TODO: keep track of handle state. If we've gotten a EOF then we should
   * not start the IO watcher.
   */
  assert(handle->fd >= 0);
  handle->read_cb = cb;

  /* These should have been set by oio_tcp_init. */
  assert(handle->read_watcher.data == handle);
  assert(handle->read_watcher.cb == oio__tcp_io);

  ev_io_start(EV_DEFAULT_UC_ &handle->read_watcher);
  return 0;
}


int oio_read_stop(oio_handle* handle) {
  oio_flag_unset(handle, OIO_READING);

  ev_io_stop(EV_DEFAULT_UC_ &handle->read_watcher);
  handle->read_cb = NULL;
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
  ngx_queue_init(&req->queue);
}
