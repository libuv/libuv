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


ol_handle* ol_tcp_new(int v4, sockaddr* addr, sockaddr_len len,
    ol_read_cb read_cb, ol_close_cb close_cb) {

  ol_handle *handle = calloc(sizeof(ol_handle), 1);
  if (!handle) {
    return NULL;
  }

  handle->read_cb = read_cb;
  handle->close_cb = close_cb;

  handle->type = v4 ? OL_TCP : OL_TCP6;

  int domain = v4 ? AF_INET : AF_INET6;
  handle->fd = socket(domain, SOCK_STREAM, 0);
  if (handle->fd == -1) {
    free(handle);
    got_error("socket", err);
    return NULL;
  }

  /* Lose the pesky "Address already in use" error message */
  int yes = 1;
  int r = setsockopt(handle->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  if (r == -1) {
    close(handle->fd);
    free(handle);
    unhandled_error("setsockopt", r);
    return NULL;
  }

  /* We auto-bind the specified address */
  if (addr) {
    int r = bind(handle->fd, addr, v4 ? sizeof(sockaddr_in) :
        sizeof(sockaddr_in6));

    if (r < 0) {
      got_error("bind", errno);
      close(handle->fd);
      free(handle);
      return NULL;
    }
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
  } else if (err != EINPROGRESS) {
    close(h->fd);
    got_error("connect", err);
  }

  /* EINPROGRESS - unlikely. What to do? */
}


ol_bucket* ol_handle_first_bucket(ol_handle* h) {
  ngx_queue_t* element = ngx_queue_head(h);
  if (!element) {
    return NULL;
  }
  return ngx_queue_data(element, ol_bucket, write_queue);
}


static void tcp_flush(oi_loop* loop, ol_handle* h) {
  ol_bucket* bucket = ol_handle_first_bucket(h);

  for (; bucket; bucket = ol_handle_first_bucket(h)) {
    io_vec* vec = (io_vec*) bucket->bufs[bucket->current_index];
    int remaining_bufcnt = bucket->bufcnt - bucket->current_index;
    ssize_t written = writev(h->fd, vec, remaining_bufcnt);

    if (written < 0) {
      if (err == EAGAIN) {
        ev_io_start(loop, &h->write_watcher);
      } else {
        got_error("writev", err);
      }

    } else {
      /* See how much was written, increase current_index, and update bufs. */
      oi_buf current = bucket->bufs[bucket->current_index];

      while (bucket->current_index < bucket->bufcnt) {
        if (current.len >= written) {
          current = bucket->bufs[++bucket->current_index];
        } else {
          bucket->bufs[bucket->current_index].buf += written;
          break;
        }
      }
    }
  }
}


static void tcp_connected(ol_handle* h) {
  assert(h->connecting);
  if (h->connect_cb) {
    h->connect_cb(h);
  }
  h->connecting = 0;
  h->connect_cb = NULL;

  ev_io_init(&h->read_watcher, tcp_io, h->fd, EV_READ);
  ev_io_start(h->loop, &h->read_watcher);

  if (ngx_queue_empty(&h->write_queue)) {
    ev_io_stop(h->loop, &h->write_watcher);
  } else {
    /* Now that we're connected let's try to flush. */
    tcp_flush(h);
  }
}


int ol_connect(ol_handle* h, sockaddr* addr, sockaddr_len addrlen,
    ol_buf* buf, ol_connect_cb connect_cb) {
  if (h->connecting) {
    return got_error("connect", EALREADY);
  }

  h->connecting = 1;
  h->connect_addr = addr;
  h->connect_addrlen = addrlen;

  if (buf) {
    /* We're allowed to ol_write before the socket becomes connected. */
    ol_write(h, buf, 1, connect_cb);
  } else {
    /* Nothing to write. Don't call the callback until we're connected. */
    h->connect_cb = connect_cb;
  }

  int r = connect(h->fd, h->connect_addr, h->connect_addrlen);

  if (r != 0) {
    if (err == EINPROGRESS) {
      /* Wait for fd to become writable. */
      h->connecting = 1;
      ev_io_init(&h->write_watcher, tcp_io, h->fd, EV_WRITE);
      ev_io_start(h->loop, &h->write_watcher);
    }

    return got_error("connect", err);
  }

  /* Connected */
  tcp_connected(h);
  return 0;
}


int ol_get_fd(ol_handle* h) {
  return h->fd;
}


ol_bucket* bucket_new(oi_handle* h, oi_buf* bufs, int bufcnt, ol_write_cb cb) {
  ol_bucket* bucket = malloc(sizeof(ol_bucket));
  if (!bucket) {
    got_error("malloc", OL_EMEM);
    return NULL;
  }

  bucket->bufs = bufs;
  bucket->bufcnt = bufcnt;
  bucket->write_cb = write_cb;
  bucket->handle = handle;
  ngx_queue_init(&bucket->write_queue);

  return bucket;
}


int ol_write(ol_handle* h, ol_buf* bufs, int bufcnt, ol_write_cb cb) {
  if (!h->connecting && !h->writable) {
    return got_error("write", OL_EPIPE);
  }

  if (!h->writable) {
    /* This happens when writing to a socket which is not connected yet. */
    bucket* b = bucket_new(h, buf, bufcnt, cb);
    bucket_append(h, b);
    return got_error("write", OL_EPENDING);
  }

  ssize_t written;

  /* If the write queue is empty, attempt to write now. */
  if (ngx_queue_empty(&h->write_queue)) {
    /* The queue is empty. Attempt the writev immediately. */
    written = writev(h->fd, (io_vec*)bufs, bufcnt);

    if (written >= 0) {
      size_t seen = 0;

      /* Figure out what's left to be written */
      for (int i = 0; i < bufcnt; i++) {
        seen += bufs[i].len;

        if (seen == written) {
          /* We wrote the entire thing. */
          return 0;
        }

        if (seen > written) {
          break;
        }
      }

      assert(seen > written);

      /* We've made a partial write of the bufs. bufs[i] is the first buf
       * that wasn't totally flushed. We must now add
       *    bufs[i], bufs[i + 1], ..., bufs[bufcnt - 1]
       * to the write queue.
       */
    }
  }


}


int ol_write2(ol_handle* h, char *base, size_t len) {
  ol_buf buf;
  buf.base = base;
  buf.len = len;

  return ol_write(h, &buf, 1, NULL);
}


int ol_write3(ol_handle* h, const char *string) {
  /* You're doing it wrong if strlen(string) > 1mb. */
  return ol_write2(h, string, strnlen(string, 1024 * 1024));
}


struct sockaddr oi_ip4_addr(char *ip, int port) {
  sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}
