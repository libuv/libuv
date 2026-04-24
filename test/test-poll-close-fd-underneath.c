/* Copyright libuv contributors. All rights reserved.
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

/* Regression test: when a Python/C caller closes a file descriptor that is
 * still registered with libuv's epoll (typically because the caller owns the
 * Python socket object lifecycle while libuv only borrows the fd), the next
 * epoll_ctl batch flush used to abort the whole process on Linux.
 *
 * Reproduces:
 *  - Register many fds via uv_poll_init/uv_poll_start so the epoll_ctl_flush
 *    batch contains real entries.
 *  - Close the underlying fds directly with close(2), bypassing uv_close.
 *  - Issue another uv_poll_start on each (changing event mask) so libuv
 *    stages epoll_ctl(MOD) operations against the now-dead fds.
 *  - Run the loop. The fds are dead; the CQEs (io_uring path) or the
 *    classic epoll_ctl call will fail with EBADF.
 *
 * Before fix: abort() at src/unix/linux.c (either rc != n on short
 *             submission, op != ADD, or cqe->res != -EEXIST).
 * After  fix: libuv silently drops the dead watchers, process stays up.
 */

#include "uv.h"
#include "task.h"

#include <errno.h>

#ifndef _WIN32
# include <fcntl.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

#define NUM_SOCKETS 64


#ifndef _WIN32
static void poll_cb(uv_poll_t* handle, int status, int events) {
  /* No-op. We never expect this to fire on a closed fd — the fd is dead. */
  (void) handle;
  (void) status;
  (void) events;
}


static void close_cb(uv_handle_t* handle) {
  (void) handle;
}


TEST_IMPL(poll_close_fd_underneath) {
  uv_loop_t* loop;
  uv_poll_t handles[NUM_SOCKETS];
  int fds[NUM_SOCKETS];
  int i;
  int r;

  loop = uv_default_loop();

  /* Create fds and register with uv_poll. */
  for (i = 0; i < NUM_SOCKETS; i++) {
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fds[i], 0);
    r = uv_poll_init(loop, &handles[i], fds[i]);
    ASSERT_OK(r);
    r = uv_poll_start(&handles[i], UV_READABLE, poll_cb);
    ASSERT_OK(r);
  }

  /* Run once so the ADD submissions flush through epoll_ctl. Returns 1
   * because handles are still active; we just need the side effect. */
  uv_run(loop, UV_RUN_NOWAIT);

  /* Pull the rug out from under libuv: close the fds directly, bypassing
   * uv_close. libuv's internal watcher table still references them. */
  for (i = 0; i < NUM_SOCKETS; i++) {
    ASSERT_OK(close(fds[i]));
  }

  /* Provoke a MOD submission against the now-dead fds. This is where the
   * abort used to fire. */
  for (i = 0; i < NUM_SOCKETS; i++) {
    r = uv_poll_start(&handles[i], UV_WRITABLE, poll_cb);
    /* uv_poll_start itself can't detect the fd is dead; the kernel will
     * reject the staged epoll_ctl on submission. The return value depends
     * on whether io_uring or classic epoll is in use, so don't assert. */
    (void) r;
  }

  /* Tick the loop so the dead submissions are processed. In unpatched
   * libuv this aborts. In patched libuv, it drops them silently. */
  r = uv_run(loop, UV_RUN_NOWAIT);
  /* Either 0 (nothing ready) or >0 (timers fired) — both fine. The point
   * is that we got here at all. */
  (void) r;

  /* Clean up. uv_close is still safe; libuv will try another epoll_ctl(DEL)
   * but that path already tolerates EBADF. */
  for (i = 0; i < NUM_SOCKETS; i++) {
    uv_close((uv_handle_t*) &handles[i], close_cb);
  }

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

#else /* _WIN32 */

TEST_IMPL(poll_close_fd_underneath) {
  RETURN_SKIP("Linux-only regression test (libuv epoll_ctl race)");
}

#endif
