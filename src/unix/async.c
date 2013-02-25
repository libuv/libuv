/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents);
static int uv__async_make_pending(int* pending);


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  if (uv__async_start(loop, &loop->async_watcher, uv__async_event))
    return uv__set_sys_error(loop, errno);

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;

  ngx_queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  if (uv__async_make_pending(&handle->pending) == 0)
    uv__async_send(&handle->loop->async_watcher);

  return 0;
}


void uv__async_close(uv_async_t* handle) {
  ngx_queue_remove(&handle->queue);
  uv__handle_stop(handle);
}


static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents) {
  ngx_queue_t* q;
  uv_async_t* h;

  ngx_queue_foreach(q, &loop->async_handles) {
    h = ngx_queue_data(q, uv_async_t, queue);
    if (!h->pending) continue;
    h->pending = 0;
    h->async_cb(h, 0);
  }
}


static int uv__async_make_pending(int* pending) {
  /* Do a cheap read first. */
  if (ACCESS_ONCE(int, *pending) != 0)
    return 1;

  /* Micro-optimization: use atomic memory operations to detect if we've been
   * preempted by another thread and don't have to make an expensive syscall.
   * This speeds up the heavily contended case by about 1-2% and has little
   * if any impact on the non-contended case.
   *
   * Use XCHG instead of the CMPXCHG that __sync_val_compare_and_swap() emits
   * on x86, it's about 4x faster. It probably makes zero difference in the
   * grand scheme of things but I'm OCD enough not to let this one pass.
   */
#if defined(__i386__) || defined(__x86_64__)
  {
    unsigned int val = 1;
    __asm__ __volatile__ ("xchgl %0, %1"
                         : "+r" (val)
                         : "m"  (*pending));
    return val != 0;
  }
#elif defined(__GNUC__) && (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ > 0)
  return __sync_val_compare_and_swap(pending, 0, 1) != 0;
#else
  ACCESS_ONCE(int, *pending) = 1;
  return 0;
#endif
}


static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  struct uv__async* wa;
  char buf[1024];
  unsigned n;
  ssize_t r;

  n = 0;
  for (;;) {
    r = read(w->fd, buf, sizeof(buf));

    if (r > 0)
      n += r;

    if (r == sizeof(buf))
      continue;

    if (r != -1)
      break;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;

    if (errno == EINTR)
      continue;

    abort();
  }

  wa = container_of(w, struct uv__async, io_watcher);
  wa->cb(loop, wa, n);
}


void uv__async_send(struct uv__async* wa) {
  int r;

  do
    r = write(wa->wfd, "", 1);
  while (r == -1 && errno == EINTR);

  if (r == 1)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

  abort();
}


void uv__async_init(struct uv__async* wa) {
  wa->io_watcher.fd = -1;
  wa->wfd = -1;
}


int uv__async_start(uv_loop_t* loop, struct uv__async* wa, uv__async_cb cb) {
  int pipefd[2];

  if (wa->io_watcher.fd != -1)
    return 0;

  if (uv__make_pipe(pipefd, UV__F_NONBLOCK))
    return -1;

  uv__io_init(&wa->io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &wa->io_watcher, UV__POLLIN);
  wa->wfd = pipefd[1];
  wa->cb = cb;

  return 0;
}


void uv__async_stop(uv_loop_t* loop, struct uv__async* wa) {
  if (wa->io_watcher.fd == -1)
    return;

  uv__io_stop(loop, &wa->io_watcher, UV__POLLIN);
  close(wa->io_watcher.fd);
  close(wa->wfd);

  wa->io_watcher.fd = -1;
  wa->wfd = -1;
}
