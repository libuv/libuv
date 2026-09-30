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
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
# include <fcntl.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

static int limit;
static int alloc;

static void* t_realloc(void* p, size_t n) {
  alloc += n;
  if (alloc > limit)
    return NULL;
  p = realloc(p, n);
  ASSERT_NOT_NULL(p);
  return p;
}

static void* t_calloc(size_t m, size_t n) {
  return t_realloc(NULL, m * n);
}

static void* t_malloc(size_t n) {
  return t_realloc(NULL, n);
}

TEST_IMPL(loop_init_oom) {
  uv_loop_t loop;
  int err;

  ASSERT_OK(uv_replace_allocator(t_malloc, t_realloc, t_calloc, free));
  for (;;) {
    err = uv_loop_init(&loop);
    if (err == 0)
      break;
    ASSERT_EQ(err, UV_ENOMEM);
    limit += 8;
    alloc = 0;
  }
  ASSERT_OK(uv_loop_close(&loop));
  return 0;
}


#ifndef _WIN32
static uv_mutex_t resize_mutex;
static void* resize_watchers;
static int resize_fail;
static int resize_poll_called;
static int resize_connection_called;


static void* resize_realloc(void* p, size_t n) {
  int fail;

  uv_mutex_lock(&resize_mutex);
  fail = resize_fail && p == resize_watchers;
  if (fail)
    resize_fail = 0;
  uv_mutex_unlock(&resize_mutex);

  return fail ? NULL : realloc(p, n);
}


static void resize_free(void* p) {
  uv_mutex_lock(&resize_mutex);
  if (resize_watchers != NULL)
    ASSERT_PTR_NE(p, resize_watchers);
  uv_mutex_unlock(&resize_mutex);
  free(p);
}


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
  int pair[2];
  int fd;
  int high_fd;
  int addrlen;
  int err;

  ASSERT_OK(uv_mutex_init(&resize_mutex));
  ASSERT_OK(uv_replace_allocator(malloc, resize_realloc, calloc, resize_free));
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
  uv_mutex_lock(&resize_mutex);
  resize_watchers = loop.watchers;
  resize_fail = 1;
  uv_mutex_unlock(&resize_mutex);
  err = uv_listen((uv_stream_t*) &server, 16, resize_connection_cb);
  uv_mutex_lock(&resize_mutex);
  ASSERT_OK(resize_fail);
  resize_watchers = NULL;
  uv_mutex_unlock(&resize_mutex);
  ASSERT_EQ(UV_ENOMEM, err);
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
  ASSERT_OK(uv_replace_allocator(malloc, realloc, calloc, free));
  uv_mutex_destroy(&resize_mutex);
  return 0;
}
#endif
