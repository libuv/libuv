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

#include <unistd.h>
#include <string.h>

static int stupid_assert_cb_calls;
static int closeme_cb_calls;

static void stupid_assert_cb(uv_handle_t* handle) {
  ASSERT(handle == handle->data);
  stupid_assert_cb_calls++;
}

static void close_cb(uv_handle_t* handle) {
  size_t handle_size;
  handle_size = uv_handle_size(handle->type);
  ASSERT(handle_size > sizeof(uv_handle_t));
  stupid_assert_cb(handle);
  memset(handle, 0, handle_size);
}

static void closeme_cb(uv_handle_t* handle, void* arg) {
  stupid_assert_cb(handle);
  ASSERT(arg == NULL);
  uv_close(handle, close_cb);
  closeme_cb_calls++;
}

static void walk_and_close_all_cb(uv_poll_t* phandle, int status, int events) {
  stupid_assert_cb((uv_handle_t*)phandle);
  ASSERT(phandle->loop == uv_default_loop());
  uv_walk(phandle->loop, closeme_cb, NULL);
}

static void nobufs_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  stupid_assert_cb(handle);
  buf->base = NULL;
  buf->len = 0;
}

#ifdef _WIN32
static int create_pipe_read_end(void) {
  HANDLE readh, writeh;
  int rfd;
  if (!CreatePipe(&readh, &writeh, NULL, 0))
    return -1;
  CloseHandle(writeh);
  rfd = _open_osfhandle((intptr_t)readh, _O_RDONLY);
  if (rfd == -1) {
    CloseHandle(readh);
    return -1;
  }
  return rfd;
}
static void close_pipe_read_end(int fd) {
  _close(fd);
}
#else
static int create_pipe_read_end(void) {
  int fds[2];
  if (pipe(fds) == -1)
    return -1;
  close(fds[1]);
  if (fcntl(fds[0], F_SETFD, O_NONBLOCK) == -1) {
    close(fds[0]);
    return -1;
  }
  return fds[0];
}
static void close_pipe_read_end(int fd) {
  close(fd);
}
#endif /* _WIN32 */


TEST_IMPL(walk_and_close_handles) {
  uv_loop_t* loop;
  uv_timer_t timer;
  uv_prepare_t prepare;
  uv_check_t check;
  uv_idle_t idle;
  uv_signal_t signl;
  uv_async_t async;
  uv_pipe_t ppe;
  uv_poll_t pll;
  uv_tcp_t tcp;
  uv_udp_t udp;
  uv_fs_poll_t fspoll;
  uv_fs_event_t fsevent;
  struct sockaddr_in addr;
  int nobjects = 0;
  int expected_stupid_assert_cb_calls = 0;
  int fd = -1;
  int r;

  loop = uv_default_loop();

  /* timer */
  r = uv_timer_init(loop, &timer);
  ASSERT(r == 0);
  timer.data = &timer;
  nobjects++;

  r = uv_timer_start(&timer, (uv_timer_cb)stupid_assert_cb, 0, 0);
  ASSERT(r == 0);
  expected_stupid_assert_cb_calls++;

  /* prepare */
  r = uv_prepare_init(loop, &prepare);
  ASSERT(r == 0);
  prepare.data = &prepare;
  nobjects++;

  r = uv_prepare_start(&prepare, (uv_prepare_cb)stupid_assert_cb);
  ASSERT(r == 0);
  expected_stupid_assert_cb_calls++;

  /* check */
  r = uv_check_init(loop, &check);
  ASSERT(r == 0);
  check.data = &check;
  nobjects++;

  r = uv_check_start(&check, (uv_check_cb)stupid_assert_cb);
  ASSERT(r == 0);

  /* idle */
  r = uv_idle_init(loop, &idle);
  ASSERT(r == 0);
  idle.data = &idle;
  nobjects++;

  r = uv_idle_start(&idle, (uv_idle_cb)stupid_assert_cb);
  ASSERT(r == 0);
  expected_stupid_assert_cb_calls++;

  /* signal */
  r = uv_signal_init(loop, &signl);
  ASSERT(r == 0);
  signl.data = &signl;
  nobjects++;

  r = uv_signal_start(&signl, (uv_signal_cb)stupid_assert_cb, SIGUSR1);
  ASSERT(r == 0);

  /* async */
  r = uv_async_init(loop, &async, (uv_async_cb)stupid_assert_cb);
  ASSERT(r == 0);
  async.data = &async;
  nobjects++;

  /* pipe/stream */
  r = uv_pipe_init(loop, &ppe, 0);
  ASSERT(r == 0);
  ppe.data = &ppe;
  nobjects++;

  r = uv_pipe_bind(&ppe, TEST_PIPENAME);
  ASSERT(r == 0);

  r = uv_listen((uv_stream_t*)&ppe, 1, (uv_connection_cb)stupid_assert_cb);
  ASSERT(r == 0);

  r = uv_read_start((uv_stream_t*)&ppe, nobufs_alloc_cb, NULL);
  ASSERT(r == 0);

  /* poll */
  fd = create_pipe_read_end();
  ASSERT(fd >= 0);
  r = uv_poll_init(loop, &pll, fd);
  ASSERT(r == 0);
  pll.data = &pll;
  nobjects++;

  r = uv_poll_start(&pll, UV_READABLE, walk_and_close_all_cb);
  ASSERT(r == 0);
  expected_stupid_assert_cb_calls++;

  /* tcp/stream */
  r = uv_tcp_init(loop, &tcp);
  ASSERT(r == 0);
  tcp.data = &tcp;
  nobjects++;

  r = uv_ip4_addr("127.0.0.1", TEST_PORT, &addr);
  ASSERT(r == 0);

  r = uv_tcp_bind(&tcp, (const struct sockaddr *)&addr, 0);
  ASSERT(r == 0);

  r = uv_listen((uv_stream_t*)&tcp, 1, (uv_connection_cb)stupid_assert_cb);
  ASSERT(r == 0);

  r = uv_read_start((uv_stream_t*)&tcp, nobufs_alloc_cb, NULL);
  ASSERT(r == 0);

  /* udp */
  r = uv_udp_init(loop, &udp);
  ASSERT(r == 0);
  udp.data = &udp;
  nobjects++;

  r = uv_udp_recv_start(&udp, nobufs_alloc_cb, (uv_udp_recv_cb)stupid_assert_cb);
  ASSERT(r == 0);

  /* fs poll */
  r = uv_fs_poll_init(loop, &fspoll);
  ASSERT(r == 0);
  fspoll.data = &fspoll;
  nobjects++;

  r = uv_fs_poll_start(&fspoll, (uv_fs_poll_cb)stupid_assert_cb, ".", 10000);
  ASSERT(r == 0);

  /* fs event */
  r = uv_fs_event_init(loop, &fsevent);
  ASSERT(r == 0);
  fsevent.data = &fsevent;
  nobjects++;

  r = uv_fs_event_start(&fsevent, (uv_fs_event_cb)stupid_assert_cb, ".", 0);
  ASSERT(r == 0);


  /* mainloop */
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(r == 0);

  ASSERT(closeme_cb_calls == nobjects);
  ASSERT(stupid_assert_cb_calls == (expected_stupid_assert_cb_calls + nobjects * 2));

  close_pipe_read_end(fd);
  fd = -1;

  MAKE_VALGRIND_HAPPY();
  ASSERT(stupid_assert_cb_calls == (expected_stupid_assert_cb_calls + nobjects * 2));

  return 0;
}
