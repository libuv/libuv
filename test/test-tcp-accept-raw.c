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

/*
 * Test: uv_tcp_accept_raw
 *
 * Verifies the zero-copy accept-and-transfer pattern:
 *   - A server loop accepts connections with uv_tcp_accept_raw, which returns
 *     the connected socket without associating it with any event loop.
 *   - The raw socket is handed to a worker loop running in a separate thread,
 *     which opens it with uv_tcp_open and reads/echoes data.
 *   - Clients in the main loop connect, send "PING", and verify the echo.
 */

#include "uv.h"
#include "task.h"

#include <string.h>

#define CONN_COUNT 5
#define PING "PING"
#define PING_LEN 4

/* ── shared transfer queue ───────────────────────────────────────────────── */

static uv_mutex_t queue_mutex;
static uv_os_sock_t queue_socks[CONN_COUNT];
static unsigned int queue_head;
static unsigned int queue_tail;

static uv_async_t worker_async;   /* signals worker loop */
static uv_async_t server_async;   /* signals server loop to stop */

/* ── server loop (thread 1) ─────────────────────────────────────────────── */

static uv_loop_t server_loop;
static uv_tcp_t  server_handle;
static uv_sem_t  server_ready;
static unsigned int server_accepted;

static void server_stop_cb(uv_async_t* handle) {
  uv_close((uv_handle_t*) handle, NULL);
  uv_close((uv_handle_t*) &server_handle, NULL);
}

static void server_connection_cb(uv_stream_t* stream, int status) {
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(status);

  r = uv_tcp_accept_raw((uv_tcp_t*) stream, &sock);
  ASSERT_OK(r);

  uv_mutex_lock(&queue_mutex);
  queue_socks[queue_tail++ % CONN_COUNT] = sock;
  uv_mutex_unlock(&queue_mutex);

  uv_async_send(&worker_async);

  server_accepted++;
  if (server_accepted == CONN_COUNT)
    uv_async_send(&server_async);
}

static void server_thread(void* arg) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_loop_init(&server_loop));

  r = uv_tcp_init(&server_loop, &server_handle);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_bind(&server_handle, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*) &server_handle, 128, server_connection_cb);
  ASSERT_OK(r);

  r = uv_async_init(&server_loop, &server_async, server_stop_cb);
  ASSERT_OK(r);

  uv_sem_post(&server_ready);

  r = uv_run(&server_loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  uv_loop_close(&server_loop);
}

/* ── worker loop (thread 2) ─────────────────────────────────────────────── */

static uv_loop_t worker_loop;
static unsigned int worker_closed;

static void worker_client_close_cb(uv_handle_t* handle) {
  free(handle);
  worker_closed++;
  if (worker_closed == CONN_COUNT)
    uv_close((uv_handle_t*) &worker_async, NULL);
}

static void worker_alloc_cb(uv_handle_t* handle,
                             size_t suggested_size,
                             uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}

typedef struct {
  uv_write_t req;
  char data[65536];
} worker_write_t;

static void worker_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  free(req);
}

static void worker_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  worker_write_t* wreq;
  uv_buf_t out;

  if (nread == UV_EOF) {
    uv_close((uv_handle_t*) stream, worker_client_close_cb);
    return;
  }

  ASSERT_GE(nread, 0);

  /* Copy into owned buffer so concurrent reads don't clobber the slab */
  wreq = malloc(sizeof(*wreq));
  ASSERT_NOT_NULL(wreq);
  memcpy(wreq->data, buf->base, (size_t) nread);
  out = uv_buf_init(wreq->data, (unsigned int) nread);
  ASSERT_OK(uv_write(&wreq->req, stream, &out, 1, worker_write_cb));
}

static void worker_async_cb(uv_async_t* handle) {
  uv_os_sock_t sock;
  uv_tcp_t* client;
  int r;

  for (;;) {
    uv_mutex_lock(&queue_mutex);
    if (queue_head == queue_tail) {
      uv_mutex_unlock(&queue_mutex);
      break;
    }
    sock = queue_socks[queue_head++ % CONN_COUNT];
    uv_mutex_unlock(&queue_mutex);

    client = malloc(sizeof(*client));
    r = uv_tcp_init(&worker_loop, client);
    ASSERT_OK(r);

    r = uv_tcp_open(client, sock);
    ASSERT_OK(r);

    r = uv_read_start((uv_stream_t*) client, worker_alloc_cb, worker_read_cb);
    ASSERT_OK(r);
  }
}

static void worker_thread(void* arg) {
  int r;

  ASSERT_OK(uv_loop_init(&worker_loop));

  r = uv_async_init(&worker_loop, &worker_async, worker_async_cb);
  ASSERT_OK(r);

  r = uv_run(&worker_loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  uv_loop_close(&worker_loop);
}

/* ── clients in main loop ───────────────────────────────────────────────── */

static uv_tcp_t     client_handles[CONN_COUNT];
static uv_connect_t connect_reqs[CONN_COUNT];
static uv_write_t   write_reqs[CONN_COUNT];
static unsigned int clients_done;

static void client_close_cb(uv_handle_t* handle) {
  (void) handle;
}

static void client_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  if (nread == UV_EOF) {
    uv_close((uv_handle_t*) stream, client_close_cb);
    return;
  }

  ASSERT_GE(nread, PING_LEN);
  ASSERT_OK(memcmp(PING, buf->base, PING_LEN));

  clients_done++;
  uv_close((uv_handle_t*) stream, client_close_cb);
}

static void client_alloc_cb(uv_handle_t* handle,
                             size_t suggested_size,
                             uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void client_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
}

static void client_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  uv_write_t* wreq;
  int i;

  ASSERT_OK(status);

  /* find the write_req index that corresponds to this handle */
  wreq = NULL;
  for (i = 0; i < CONN_COUNT; i++) {
    if (req == &connect_reqs[i]) {
      wreq = &write_reqs[i];
      break;
    }
  }
  ASSERT_NOT_NULL(wreq);

  buf = uv_buf_init(PING, PING_LEN);
  ASSERT_OK(uv_write(wreq, req->handle, &buf, 1, client_write_cb));
  ASSERT_OK(uv_read_start(req->handle, client_alloc_cb, client_read_cb));
}

/* ── test entry point ───────────────────────────────────────────────────── */

TEST_IMPL(tcp_accept_raw) {
  struct sockaddr_in addr;
  uv_thread_t server_tid;
  uv_thread_t worker_tid;
  int i;
  int r;

  ASSERT_OK(uv_mutex_init(&queue_mutex));
  ASSERT_OK(uv_sem_init(&server_ready, 0));

  /* Start background threads */
  ASSERT_OK(uv_thread_create(&worker_tid, worker_thread, NULL));
  ASSERT_OK(uv_thread_create(&server_tid, server_thread, NULL));

  /* Wait until server is listening */
  uv_sem_wait(&server_ready);

  /* Connect CONN_COUNT clients from the main loop */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  for (i = 0; i < CONN_COUNT; i++) {
    r = uv_tcp_init(uv_default_loop(), &client_handles[i]);
    ASSERT_OK(r);

    r = uv_tcp_connect(&connect_reqs[i],
                       &client_handles[i],
                       (const struct sockaddr*) &addr,
                       client_connect_cb);
    ASSERT_OK(r);
  }

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  uv_thread_join(&server_tid);
  uv_thread_join(&worker_tid);

  ASSERT_EQ(clients_done, CONN_COUNT);
  ASSERT_EQ(server_accepted, CONN_COUNT);
  ASSERT_EQ(worker_closed, CONN_COUNT);

  uv_mutex_destroy(&queue_mutex);
  uv_sem_destroy(&server_ready);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
