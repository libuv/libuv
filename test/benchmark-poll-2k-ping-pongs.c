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

#include "uv.h"
#include "task.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/* Run the benchmark for this many ms */
#define TIME 15000
#define NUM_POLLS 2000

typedef struct {
  int pings;
  ssize_t read_state;
  int pongs;
  ssize_t write_state;
  int sock_state;
  int socket_fd;
  uv_poll_t poll;
} pinger_t;

static uint64_t all_pings = 0;
static uint64_t all_pongs = 0;

static pinger_t pingers[NUM_POLLS];

static char PING[] = "PING\n";

static uv_loop_t* loop;

static int completed_pingers = 0;
static int64_t start_time;

static void close_handles() {
  pinger_t *pinger;

  for (pinger = pingers; pinger < pingers + NUM_POLLS; ++pinger) {
    if (pinger->sock_state > 0)
      close(pinger->socket_fd);
  }
}

static void pinger_close_cb(uv_handle_t* handle) {
  pinger_t * p;

  completed_pingers++;
  p = container_of(handle, pinger_t, poll);
  close(p->socket_fd);
  all_pings += p->pings;
  all_pongs += p->pongs;
}

static void poll_cb(uv_poll_t* handle, int status, int events);

static void close_poll(uv_poll_t* handle) {
  int r;

  r = uv_poll_stop(handle);
  ASSERT(!r);
  uv_close((uv_handle_t*)handle, pinger_close_cb);
  return;
}

static void pinger_write_cb(uv_poll_t* handle) {
  pinger_t * p;
  ssize_t written;
  int r;
  ssize_t max_written;

  p = container_of(handle, pinger_t, poll);
  max_written = sizeof(PING) - p->write_state - 1;
  written = write(p->socket_fd, PING + p->write_state, max_written);
  ASSERT(written >= 0);
  if (written == max_written) {
    // switch to read
    p->write_state = 0;
    p->pings++;
    r = uv_poll_start(handle, UV_READABLE, poll_cb);
    ASSERT(!r);
  } else {
    p->write_state += written;
  }
}

static void pinger_read_cb(uv_poll_t* handle) {
  ssize_t nread;
  ssize_t max_read;
  pinger_t* p;
  int r;
  static char buf[sizeof(PING)];

  p = container_of(handle, pinger_t, poll);
  nread = read(p->socket_fd, buf, sizeof(buf));
  ASSERT(read >= 0);
  max_read = sizeof(PING) - p->read_state - 1;
  if (nread == max_read) {
    p->read_state = 0;
    p->pongs++;
    r = uv_poll_start(handle, UV_WRITABLE, poll_cb);
    ASSERT(!r);
  } else {
    p->read_state += nread;
  }
}

void poll_cb(uv_poll_t* handle, int status, int events) {
  if (status < 0) {
    fprintf(stderr, "%s\n", uv_strerror(status));
    fflush(stderr);
    close_poll(handle);
    return;
  }
  if (uv_now(loop) - start_time > TIME) {
    close_poll(handle);
    return;
  }
  ASSERT(events == UV_READABLE || events == UV_WRITABLE);
  if (events & UV_READABLE)
    pinger_read_cb(handle);
  else // if (events & UV_WRITABLE)
    pinger_write_cb(handle);
}

static void poll_connect_cb(uv_poll_t* handle, int status, int events) {
  pinger_t* p;
  int socket_error;
  int r;
  if (status < 0) {
    fprintf(stderr, "%s\n", uv_strerror(status));
    fflush(stderr);
    close_poll(handle);
    return;
  }
  if (uv_now(loop) - start_time > TIME) {
    close_poll(handle);
    return;
  }
  p = container_of(handle, pinger_t, poll);
  if (events & UV_WRITABLE) {
    socklen_t len = sizeof(int);
    r = getsockopt(p->socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);
    if (r || socket_error) {
      if (!r)
        errno = socket_error;
      perror("connect");
      close_poll(handle);
      return;
    }
  }
  p->sock_state = 2;
  status = uv_poll_start(handle, UV_WRITABLE, poll_cb);
  if (status < 0) {
    fprintf(stderr, "uv_poll_start: %s\n", uv_strerror(status));
    close_poll(handle);
    return;
  }
}

static int pinger_new(void) {
  struct sockaddr_in server_addr;
  pinger_t *pinger;

  // connection is in benchmark beacuase async as well
  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  for (pinger = pingers; pinger < pingers + NUM_POLLS; ++pinger) {
    pinger->socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (pinger->socket_fd == -1) {
      perror("socket");
      return 1;
    }
    if (uv_poll_init(loop, &pinger->poll, pinger->socket_fd))
      return 1;

    if (connect(pinger->socket_fd, &server_addr, sizeof(server_addr)) < 0) {
      if (errno != EINPROGRESS) {
        perror("connect");
        return 1;
      }
      if (uv_poll_start(&pinger->poll, UV_WRITABLE, poll_connect_cb))
        return 1;
    } else {
      pinger->sock_state = 2;
      if (uv_poll_start(&pinger->poll, UV_WRITABLE, poll_cb))
        return 1;
    }
    pinger->sock_state = 1;
    pinger->read_state = 0;
    pinger->write_state = 0;
    pinger->pongs = 0;
    pinger->pings = 0;
  }
  return 0;
}

BENCHMARK_IMPL(poll_2k_ping_pongs) {
  loop = uv_default_loop();

  if (pinger_new()) {
    close_handles();
    return 1;
  }
  start_time = uv_now(loop);
  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(completed_pingers == NUM_POLLS);
  fprintf(stderr, "pings: %lu roundtrips/s\n", (1000 * all_pings) / TIME);
  fprintf(stderr, "pongs: %lu roundtrips/s\n", (1000 * all_pongs) / TIME);
  fflush(stderr);
  MAKE_VALGRIND_HAPPY();
  return 0;
}
