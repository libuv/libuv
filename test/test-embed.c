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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef HAVE_EPOLL
# if defined(__linux__)
#  define HAVE_EPOLL 1
# endif
#endif

#if defined(HAVE_EPOLL)
# include <sys/epoll.h>
#endif

#if !defined(_WIN32)
# include <sys/types.h>
# include <sys/time.h>
#endif

static uv_loop_t main_loop;
static uv_loop_t external_loop;
static uv_thread_t embed_thread;
static uv_sem_t embed_sem;
static uv_async_t embed_async;
static volatile int embed_closed;

static uv_timer_t main_timer;
static int main_timer_called;


#if defined(_WIN32)
static void embed_thread_poll_win(HANDLE iocp, int timeout) {
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* overlapped;

  GetQueuedCompletionStatus(iocp,
                            &bytes,
                            &key,
                            &overlapped,
                            timeout >= 0 ? timeout : INFINITE);

  /* Give the event back so the loop can deal with it. */
  if (overlapped != NULL)
    PostQueuedCompletionStatus(iocp,
                               bytes,
                               key,
                               overlapped);
}
#else
static void embed_thread_poll_unix(int fd, int timeout) {
  int r;
  do {
#if defined(HAVE_EPOLL)
    struct epoll_event ev;
    r = epoll_wait(fd, &ev, 1, timeout);
#else
    struct timeval tv;
    if (timeout >= 0) {
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = (timeout % 1000) * 1000;
    }
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(fd, &readset);
    r = select(fd + 1, &readset, NULL, NULL, timeout >= 0 ? &tv : NULL);
#endif
  } while (r == -1 && errno == EINTR);
}
#endif /* !_WIN32 */


static void embed_thread_runner(void* arg) {
  uv_os_fd_t fd;
  int timeout;

  while (!embed_closed) {
    uv_sem_wait(&embed_sem);

    fd = uv_backend_fd(&main_loop);
    timeout = uv_backend_timeout(&main_loop);

#if defined(_WIN32)
    embed_thread_poll_win(fd, timeout);
#else
    embed_thread_poll_unix(fd, timeout);
#endif

    uv_async_send(&embed_async);
  }
}


static void embed_cb(uv_async_t* async) {
  /* Run tasks in main loop */
  uv_run(&main_loop, UV_RUN_NOWAIT);

  /* Tell embed thread to continue polling */
  uv_sem_post(&embed_sem);
}


static void main_timer_cb(uv_timer_t* timer) {
  main_timer_called++;
  embed_closed = 1;

  uv_close((uv_handle_t*) &embed_async, NULL);
}


static void init_loops() {
  ASSERT(0 == uv_loop_init(&main_loop));
  ASSERT(0 == uv_loop_init(&external_loop));

  main_timer_called = 0;
  embed_closed = 0;

  uv_async_init(&external_loop, &embed_async, embed_cb);

  /* Start worker that will poll main loop and interrupt external loop */
  uv_sem_init(&embed_sem, 0);
  uv_thread_create(&embed_thread, embed_thread_runner, NULL);
}


static void run_loop() {
  /* Run main loop for once to give things a chance to initialize */
  embed_cb(&embed_async);

  /* Run external loop */
  uv_run(&external_loop, UV_RUN_DEFAULT);

  uv_thread_join(&embed_thread);
  uv_sem_destroy(&embed_sem);
  uv_loop_close(&external_loop);
  uv_loop_close(&main_loop);
}


TEST_IMPL(embed) {
  init_loops();

  /* Start timer in main loop */
  uv_timer_init(&main_loop, &main_timer);
  uv_timer_start(&main_timer, main_timer_cb, 250, 0);

  run_loop();
  ASSERT(main_timer_called == 1);

  return 0;
}


static uv_async_t main_async;
static uv_timer_t external_timer;


static void wakeup(uv_async_t* async) {
  uv_close((uv_handle_t*) &main_async, NULL);
}


static void timer_started_cb(uv_loop_t* loop) {
  /* Interrupt the polling in embed thread so backend timeout is updated */
  uv_async_send(&main_async);
}


static void external_timer_cb(uv_timer_t* timer) {
  /* Start timer in main loop */
  uv_timer_init(&main_loop, &main_timer);
  uv_timer_start(&main_timer, main_timer_cb, 100, 0);
}


TEST_IMPL(embed_with_external_timer) {
  init_loops();

  uv_async_init(&main_loop, &main_async, wakeup);

  /* Monitor when a timer is started in main loop */
  uv_loop_set_timer_started_callback(&main_loop, timer_started_cb);

  /* Start timer in external loop, whose callback will not interrupt the
     polling in embed thread */
  uv_timer_init(&external_loop, &external_timer);
  uv_timer_start(&external_timer, external_timer_cb, 100, 0);

  run_loop();
  ASSERT(main_timer_called == 1);

  return 0;
}
