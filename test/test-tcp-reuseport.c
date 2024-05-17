/* Copyright libuv project contributors. All rights reserved.
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
#include <string.h>

#if !defined(__linux__) && !defined(__FreeBSD__) && \
    !defined(__DragonFly__) && !defined(__sun) && !defined(_AIX73)

TEST_IMPL(tcp_reuseport) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uv_tcp_t handle;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_tcp_init(loop, &handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(&handle, (const struct sockaddr*) &addr, UV_TCP_REUSEPORT);
  ASSERT_EQ(r, UV_ENOTSUP);

  MAKE_VALGRIND_HAPPY(loop);

  return 0;
}

#else

#define MAX_TCP_CLIENTS 10

static uv_tcp_t tcp_connect_handles[MAX_TCP_CLIENTS];
static uv_connect_t tcp_connect_requests[MAX_TCP_CLIENTS];

static unsigned int main_loop_accepted;
static unsigned int thread_loop_accepted;
static unsigned int connected;

static uv_mutex_t mutex;
static unsigned int accepted;

static uv_loop_t* main_loop;
static uv_loop_t* thread_loop;
static uv_tcp_t main_handle;
static uv_tcp_t thread_handle;
static uv_timer_t main_timer_handle;
static uv_timer_t thread_timer_handle;

static void on_close(uv_handle_t* handle) {
  free(handle);
}

static void ticktack(uv_timer_t* timer) {
  ASSERT(timer == &main_timer_handle || timer == &thread_timer_handle);

  int done = 0;
  uv_mutex_lock(&mutex);
  if (accepted == MAX_TCP_CLIENTS) {
    done = 1;
  }
  uv_mutex_unlock(&mutex);

  if (done) {
    uv_close((uv_handle_t*) timer, NULL);
    if (timer->loop == main_loop)
      uv_close((uv_handle_t*) &main_handle, NULL);
    if (timer->loop == thread_loop)
      uv_close((uv_handle_t*) &thread_handle, NULL);
  }
}

static void on_connection(uv_stream_t* server, int status)
{
  ASSERT_OK(status);
  ASSERT(server == (uv_stream_t*) &main_handle || \
         server == (uv_stream_t*) &thread_handle);

  uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
  ASSERT_OK(uv_tcp_init(server->loop, client));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) client));
  uv_close((uv_handle_t*) client, on_close);

  if (server->loop == main_loop)
    main_loop_accepted++;

  if (server->loop == thread_loop)
    thread_loop_accepted++;

  uv_mutex_lock(&mutex);
  accepted++;
  uv_mutex_unlock(&mutex);
}

static void on_connect(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_NOT_NULL(req->handle);
  ASSERT_PTR_EQ(req->handle->loop, main_loop);

  connected++;
  uv_close((uv_handle_t*) req->handle, NULL);
}

static void run_event_loop(void* arg) {
  int r;
  uv_loop_t* loop = (uv_loop_t*) arg;
  ASSERT_PTR_EQ(loop, thread_loop);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
}

static void create_listener(uv_loop_t* loop, uv_tcp_t* handle) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(handle, (const struct sockaddr*) &addr, UV_TCP_REUSEPORT);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*) handle, 128, on_connection);
  ASSERT_OK(r);
}

TEST_IMPL(tcp_reuseport) {
  struct sockaddr_in addr;
  int r;

  r = uv_mutex_init(&mutex);

  /* Create listener per event loop. */
  main_loop = uv_default_loop();
  ASSERT_NOT_NULL(main_loop);
  create_listener(main_loop, &main_handle);
  uv_timer_init(main_loop, &main_timer_handle);
  uv_timer_start(&main_timer_handle, ticktack, 0, 10);

  thread_loop = uv_loop_new();
  ASSERT_NOT_NULL(thread_loop);
  create_listener(thread_loop, &thread_handle);
  uv_timer_init(thread_loop, &thread_timer_handle);
  uv_timer_start(&thread_timer_handle, ticktack, 0, 10);

  /* Connect to the peers. */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  int i;
  for (i = 0; i < MAX_TCP_CLIENTS; i++) {
    r = uv_tcp_init(main_loop, &tcp_connect_handles[i]);
    ASSERT_OK(r);
    r = uv_tcp_connect(&tcp_connect_requests[i],
                       &tcp_connect_handles[i],
                       (const struct sockaddr*) &addr,
                       on_connect);
    ASSERT_OK(r);
  }

  /* Run event loops and wait for them to exit. */
  uv_thread_t thread_loop_id;
  uv_thread_create(&thread_loop_id, run_event_loop, thread_loop);

  r = uv_run(main_loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  uv_thread_join(&thread_loop_id);

  /* Verify if each listener per event loop accepted connections
   * and the amount of accepted connections matches the one of
   * connected connections.
   */
  ASSERT_EQ(accepted, MAX_TCP_CLIENTS);
  ASSERT_EQ(connected, MAX_TCP_CLIENTS);
  ASSERT_GT(main_loop_accepted, 0);
  ASSERT_GT(thread_loop_accepted, 0);
  ASSERT_EQ(main_loop_accepted + thread_loop_accepted, connected);

  /* Clean up. */
  uv_mutex_destroy(&mutex);

  uv_loop_delete(thread_loop);
  MAKE_VALGRIND_HAPPY(main_loop);

  return 0;
}

#endif
