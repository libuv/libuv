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

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

static int uv__async_init(uv_loop_t* loop);
static void uv__async_io(uv_loop_t* loop, uv__io_t* handle, int events);


static int uv__async_make_pending(volatile sig_atomic_t* ptr) {
  /* Do a cheap read first. */
  if (*ptr)
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
#if __i386__ || __x86_64__
  {
    unsigned int val = 1;
    __asm__ __volatile__("xchgl %0, %1" : "+r" (val) : "m" (*ptr));
    return val != 0;
  }
#elif __GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 1 /* gcc >= 4.1 */
  return __sync_val_compare_and_swap(ptr, 0, 1) != 0;
#else
  *ptr = 1;
  return 0;
#endif
}


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  if (uv__async_init(loop))
    return uv__set_sys_error(loop, errno);

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;

  ngx_queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  int r;

  if (uv__async_make_pending(&handle->pending))
    return 0; /* already pending */

  do
    r = write(handle->loop->async_pipefd[1], &handle, sizeof(&handle));
  while (r == -1 && errno == EINTR);

  if (r == -1) {
    /* If pipe is full, event loop should sweep all handles */
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      handle->loop->async_sweep_needed = 1;

      /* Pipe might be drained at this stage, which means that
       * async_sweep_needed flag wasn't seen yet. Try writing pointer
       * once again to ensure that pipe isn't drained yet, or to invoke
       * uv__async_io.
       */
      do
        r = write(handle->loop->async_pipefd[1], &handle, sizeof(&handle));
      while (r == -1 && errno == EINTR);
    } else
      return uv__set_sys_error(handle->loop, errno);
  }

  return 0;
}


void uv__async_close(uv_async_t* handle) {
  ngx_queue_remove(&handle->queue);
  uv__handle_stop(handle);
}


static int uv__async_init(uv_loop_t* loop) {
  if (loop->async_pipefd[0] != -1)
    return 0;

  if (uv__make_pipe(loop->async_pipefd, UV__F_NONBLOCK))
    return -1;

  uv__io_init(&loop->async_watcher,
              uv__async_io,
              loop->async_pipefd[0],
              UV__IO_READ);
  uv__io_start(loop, &loop->async_watcher);

  return 0;
}


#define ASYNC_CB(a) \
    if (((a)->flags & (UV_CLOSING | UV_CLOSED)) == 0 && (a)->pending) { \
      (a)->pending = 0; \
      (a)->async_cb((a), 0); \
    }


static void uv__async_io(uv_loop_t* loop, uv__io_t* handle, int events) {
  char buf[1024];
  ngx_queue_t* q;
  uv_async_t* h;
  ssize_t r;
  ssize_t i;
  ssize_t bytes;
  ssize_t end;

  bytes = 0;
  while (1) {
    r = read(loop->async_pipefd[0], buf + bytes, sizeof(buf) - bytes);

    if (r == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Spin if there're partial data in buffer already */
        if (bytes)
          continue;
        break;
      }

      if (errno == EINTR)
        continue;

      abort();
    }

    bytes += r;

    /* Round up end */
    end = (bytes / sizeof(h)) * sizeof(h);

    /* If we need to sweep all handles anyway - skip this loop */
    if (!loop->async_sweep_needed) {
      for (i = 0; i < end; i += sizeof(h)) {
        h = *((uv_async_t**) (buf + i));
        ASYNC_CB(h)
      }
    }

    bytes -= end;

    /* Partial read happened */
    if (bytes) {
      memmove(buf, buf + end, bytes);
      /* Spin */
      continue;
    }

    if (r != sizeof(buf))
      break;
  }

  /* Fast path: no sweep is required */
  if (!loop->async_sweep_needed)
    return;

  /* Slow path: sweep all handles */
  loop->async_sweep_needed = 0;

  ngx_queue_foreach(q, &loop->async_handles) {
    h = ngx_queue_data(q, uv_async_t, queue);
    ASYNC_CB(h)
  }
}
