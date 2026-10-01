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

#include "uv.h"
#include "task.h"

#ifndef _WIN32
# include <fcntl.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

TEST_IMPL(loop_init_oom) {
  uv_loop_t loop;
  int skip;
  int err;

  oom_init();
  /* Fail every allocation after the first `skip` until init succeeds. */
  for (skip = 0; ; skip++) {
    oom_fail(skip, -1);
    err = uv_loop_init(&loop);
    if (err == 0)
      break;
    ASSERT_EQ(err, UV_ENOMEM);
    ASSERT_GT(oom_failures(), 0);
    ASSERT_OK(oom_live());
  }
  ASSERT_OK(oom_failures());
  oom_fail(0, 0);
  ASSERT_OK(uv_loop_close(&loop));
  ASSERT_OK(oom_live());
  oom_cleanup();
  return 0;
}


#ifndef _WIN32
static int resize_poll_called;
static int resize_connection_called;


static void resize_poll_cb(uv_poll_t* handle, int status, int events) {
  ASSERT_OK(status);
  ASSERT_EQ(events, UV_READABLE);
  resize_poll_called++;
  uv_close((uv_handle_t*) handle, NULL);
}


static void resize_connection_cb(uv_stream_t* handle, int status) {
  ASSERT_OK(status);
  resize_connection_called++;
  uv_close((uv_handle_t*) handle, NULL);
}


TEST_IMPL(loop_watcher_resize_oom) {
  struct sockaddr_in addr;
  uv_loop_t loop;
  uv_poll_t poll_handle;
  uv_tcp_t server;
  void* watchers;
  int pair[2];
  int live;
  int fd;
  int high_fd;
  int addrlen;
  int err;

  oom_init();
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
  ASSERT_OK(uv_poll_init(&loop, &poll_handle, pair[0]));
  ASSERT_OK(uv_poll_start(&poll_handle, UV_READABLE, resize_poll_cb));
  ASSERT_EQ(1, uv_run(&loop, UV_RUN_NOWAIT));

  fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  high_fd = fcntl(fd, F_DUPFD, loop.nwatchers);
  ASSERT_GE(high_fd, 0);
  ASSERT_OK(close(fd));
  ASSERT_OK(uv_tcp_init(&loop, &server));
  ASSERT_OK(uv_tcp_open(&server, high_fd));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0));

  /* A failed resize must leave the existing watcher table owned by the loop. */
  watchers = loop.watchers;
  live = oom_live();
  oom_fail(0, 1);
  err = uv_listen((uv_stream_t*) &server, 16, resize_connection_cb);
  ASSERT_EQ(1, oom_failures());
  ASSERT_EQ(UV_ENOMEM, err);
  ASSERT_PTR_EQ(watchers, loop.watchers);
  ASSERT_EQ(live, oom_live());
  ASSERT_OK(uv_is_active((uv_handle_t*) &server));
  ASSERT_OK(server.io_watcher.pevents);

  /* Watchers registered before the failed resize must still deliver events. */
  ASSERT_EQ(1, write(pair[1], "x", 1));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, resize_poll_called);
  ASSERT_OK(close(pair[0]));
  ASSERT_OK(close(pair[1]));

  /* Retrying the failed registration must work once memory is available. */
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 16, resize_connection_cb));
  addrlen = sizeof(addr);
  ASSERT_OK(uv_tcp_getsockname(&server, (struct sockaddr*) &addr, &addrlen));
  fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  ASSERT_OK(connect(fd, (const struct sockaddr*) &addr, sizeof(addr)));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, resize_connection_called);
  ASSERT_OK(close(fd));
  ASSERT_OK(uv_loop_close(&loop));
  oom_cleanup();
  return 0;
}
#endif
