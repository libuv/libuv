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

/* Regression tests for kqueue deleting an unwanted filter without clearing
 * the corresponding event bit in w->events:
 * 1. watcher_write_restart_after_full_poll: EVFILT_WRITE is stopped in a full
 *    1024-event kevent() batch, deleted on the immediate re-poll, and started
 *    again before watcher_queue is flushed.
 * 2. watcher_read_restart_same_poll: EVFILT_READ is stopped by an earlier
 *    callback in the same poll batch, deleted on delivery, and started again
 *    before watcher_queue is flushed.
 */

#include "uv.h"
#include "task.h"

#include <string.h>

#ifndef _WIN32
# include <sys/socket.h>
# include <unistd.h>
#endif

static char fill_chunk[65536];
static char small_chunk[4096];
static char read_slab[65536];


static void close_socket(uv_os_sock_t fd) {
#ifdef _WIN32
  ASSERT_OK(closesocket(fd));
#else
  ASSERT_OK(close(fd));
#endif
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  *buf = uv_buf_init(read_slab, sizeof(read_slab));
}


static void fill_send_buffer(uv_tcp_t* stream) {
  uv_buf_t buf;
  int i;
  int r;

  buf = uv_buf_init(fill_chunk, sizeof(fill_chunk));
  for (i = 0; i < 1024; i++) {
    r = uv_try_write((uv_stream_t*) stream, &buf, 1);
    if (r == UV_EAGAIN || r == UV_ENOSYS)
      break;
    ASSERT_GT(r, 0);
  }

  buf = uv_buf_init(small_chunk, sizeof(small_chunk));
  for (i = 0; i < 1024; i++) {
    r = uv_try_write((uv_stream_t*) stream, &buf, 1);
    if (r == UV_EAGAIN || r == UV_ENOSYS)
      break;
    ASSERT_GT(r, 0);
  }
}


/* --- Test 1: watcher_write_restart_after_full_poll --- */

/* 300 socketpairs = 600 sockets watching UV_READABLE | UV_WRITABLE, which
 * produces 1200 ready kevents (> 1024) on kqueue while staying below a 1024
 * file descriptor limit.
 */
#define NUM_FILLER_PAIRS 300
#define NUM_FILLERS (NUM_FILLER_PAIRS * 2)

static uv_tcp_t wf_server;
static uv_tcp_t wf_client;
static uv_tcp_t wf_peer;
static uv_connect_t wf_connect_req;
static uv_write_t wf_write1_req;
static uv_write_t wf_write2_req;
static uv_idle_t wf_idle;
static uv_check_t wf_check;
static uv_timer_t wf_watchdog;
static uv_poll_t wf_fillers[NUM_FILLERS];
static uv_os_sock_t wf_filler_fds[NUM_FILLERS];
static int wf_connected;
static int wf_phase;
static int wf_write1_done;
static int wf_write2_done;
static int wf_fillers_active;
static int wf_close_cb_called;


static void wf_close_cb(uv_handle_t* handle) {
  wf_close_cb_called++;
}


static void wf_stop_all_fillers(void) {
  int i;

  if (!wf_fillers_active)
    return;

  for (i = 0; i < NUM_FILLERS; i++)
    ASSERT_OK(uv_poll_stop(&wf_fillers[i]));
  wf_fillers_active = 0;
}


static void wf_shutdown_test(void) {
  int i;

  wf_stop_all_fillers();
  ASSERT_OK(uv_timer_stop(&wf_watchdog));
  ASSERT_OK(uv_check_stop(&wf_check));
  ASSERT_OK(uv_idle_stop(&wf_idle));

  uv_close((uv_handle_t*) &wf_watchdog, wf_close_cb);
  uv_close((uv_handle_t*) &wf_check, wf_close_cb);
  uv_close((uv_handle_t*) &wf_idle, wf_close_cb);
  uv_close((uv_handle_t*) &wf_client, wf_close_cb);
  uv_close((uv_handle_t*) &wf_peer, wf_close_cb);
  uv_close((uv_handle_t*) &wf_server, wf_close_cb);
  for (i = 0; i < NUM_FILLERS; i++)
    uv_close((uv_handle_t*) &wf_fillers[i], wf_close_cb);
}


static void wf_watchdog_cb(uv_timer_t* handle) {
  fprintf(stderr,
          "wf stall: phase=%d w1=%d w2=%d wq=%" PRIu64 "\n",
          wf_phase,
          wf_write1_done,
          wf_write2_done,
          (uint64_t) wf_client.write_queue_size);
  FATAL("write stalled after full poll batch");
}


static void wf_idle_cb(uv_idle_t* handle) {
  /* Keeps uv__io_poll timeout at 0 for one tick to flush watcher_queue. */
}


static void wf_client_read_cb(uv_stream_t* stream,
                              ssize_t nread,
                              const uv_buf_t* buf) {
  ASSERT_GE(nread, 0);
}


static void wf_peer_read_cb(uv_stream_t* stream,
                            ssize_t nread,
                            const uv_buf_t* buf) {
  ASSERT_GE(nread, 0);
}


static void wf_filler_poll_cb(uv_poll_t* handle, int status, int events) {
  ASSERT_OK(status);
  if (wf_write1_done)
    ASSERT_OK(uv_poll_stop(handle));
}


static void wf_write1_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  wf_write1_done = 1;
}


static void wf_write2_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  wf_write2_done = 1;
  wf_shutdown_test();
}


static void wf_check_cb(uv_check_t* handle);


static void wf_start_phase1(void) {
  uv_buf_t bufs[16];
  int i;

  /* Keep POLLIN active on wf_client so stopping POLLOUT leaves pevents != 0. */
  ASSERT_OK(uv_read_start((uv_stream_t*) &wf_client,
                          alloc_cb,
                          wf_client_read_cb));

  fill_send_buffer(&wf_client);
  for (i = 0; i < (int) ARRAY_SIZE(bufs); i++)
    bufs[i] = uv_buf_init(fill_chunk, sizeof(fill_chunk));
  ASSERT_OK(uv_write(&wf_write1_req,
                     (uv_stream_t*) &wf_client,
                     bufs,
                     ARRAY_SIZE(bufs),
                     wf_write1_cb));
#ifndef _WIN32
  ASSERT_GT(wf_client.write_queue_size, 0);
#endif

  /* Wait one full poll cycle with timeout=0 (via wf_idle) so watcher_queue is
   * flushed and EVFILT_WRITE is registered (w->events |= POLLOUT) before we
   * trigger the full 1024-event batch.
   */
  wf_phase = 1;
  ASSERT_OK(uv_idle_start(&wf_idle, wf_idle_cb));
  ASSERT_OK(uv_check_start(&wf_check, wf_check_cb));
}


static void wf_check_cb(uv_check_t* handle) {
  uv_buf_t bufs[16];
  int i;

  if (wf_phase == 1) {
    /* Still in the tick where wf_start_phase1() ran; wait for next poll. */
    wf_phase = 2;
    return;
  }

  if (wf_phase == 2) {
    /* watcher_queue has now been flushed: EVFILT_WRITE is registered on
     * wf_client. Stop idle, start reading on wf_peer, and arm >1024 ready
     * filler events so wf_write1 completes inside a full 1024-event batch and
     * the immediate re-poll deletes EVFILT_WRITE.
     */
    ASSERT_OK(uv_idle_stop(&wf_idle));
    ASSERT_OK(uv_read_start((uv_stream_t*) &wf_peer,
                            alloc_cb,
                            wf_peer_read_cb));
    for (i = 0; i < NUM_FILLERS; i++) {
      ASSERT_OK(uv_poll_start(&wf_fillers[i],
                              UV_READABLE | UV_WRITABLE,
                              wf_filler_poll_cb));
    }
    wf_fillers_active = 1;
    wf_phase = 3;
    return;
  }

  if (wf_phase == 3 && wf_write1_done) {
    /* We are still in the same loop iteration right after uv__io_poll()
     * returned, BEFORE the next uv__io_poll() flushes watcher_queue.
     * Queue a second blocked write so uv__write() calls uv__io_start(POLLOUT).
     */
    wf_stop_all_fillers();
    ASSERT_OK(uv_read_stop((uv_stream_t*) &wf_peer));

    fill_send_buffer(&wf_client);
    for (i = 0; i < (int) ARRAY_SIZE(bufs); i++)
      bufs[i] = uv_buf_init(fill_chunk, sizeof(fill_chunk));
    ASSERT_OK(uv_write(&wf_write2_req,
                       (uv_stream_t*) &wf_client,
                       bufs,
                       ARRAY_SIZE(bufs),
                       wf_write2_cb));
#ifndef _WIN32
    ASSERT_GT(wf_client.write_queue_size, 0);
#endif

    ASSERT_OK(uv_read_start((uv_stream_t*) &wf_peer,
                            alloc_cb,
                            wf_peer_read_cb));
    ASSERT_OK(uv_check_stop(&wf_check));
    wf_phase = 4;
  }
}


static void wf_maybe_start_phase1(void) {
  if (++wf_connected == 2)
    wf_start_phase1();
}


static void wf_connect_cb(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  wf_maybe_start_phase1();
}


static void wf_connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(server->loop, &wf_peer));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &wf_peer));
  wf_maybe_start_phase1();
}


TEST_IMPL(watcher_write_restart_after_full_poll) {
  struct sockaddr_in addr;
  uv_os_sock_t pair[2];
  uv_loop_t* loop;
  int bufsize;
  int i;

  TEST_FILE_LIMIT(NUM_FILLERS + 64);

  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_idle_init(loop, &wf_idle));
  ASSERT_OK(uv_check_init(loop, &wf_check));
  ASSERT_OK(uv_timer_init(loop, &wf_watchdog));
  ASSERT_OK(uv_timer_start(&wf_watchdog, wf_watchdog_cb, 3500, 0));

  for (i = 0; i < NUM_FILLER_PAIRS; i++) {
    ASSERT_OK(uv_socketpair(SOCK_STREAM,
                            0,
                            pair,
                            UV_NONBLOCK_PIPE,
                            UV_NONBLOCK_PIPE));
    ASSERT_EQ(1, send(pair[0], "x", 1, 0));
    ASSERT_EQ(1, send(pair[1], "x", 1, 0));
    wf_filler_fds[2 * i] = pair[0];
    wf_filler_fds[2 * i + 1] = pair[1];
    ASSERT_OK(uv_poll_init_socket(loop, &wf_fillers[2 * i], pair[0]));
    ASSERT_OK(uv_poll_init_socket(loop, &wf_fillers[2 * i + 1], pair[1]));
  }

  ASSERT_OK(uv_tcp_init(loop, &wf_server));
  ASSERT_OK(uv_tcp_bind(&wf_server, (const struct sockaddr*) &addr, 0));
  /* Keep socket buffers >= 2x Linux loopback MTU (65536) and set SO_RCVBUF
   * before listen() so TCP window scaling negotiates cleanly on Linux.
   */
  bufsize = 131072;
  ASSERT_OK(uv_recv_buffer_size((uv_handle_t*) &wf_server, &bufsize));
  ASSERT_OK(uv_listen((uv_stream_t*) &wf_server, 1, wf_connection_cb));

  ASSERT_OK(uv_tcp_init(loop, &wf_client));
  ASSERT_OK(uv_tcp_connect(&wf_connect_req,
                           &wf_client,
                           (const struct sockaddr*) &addr,
                           wf_connect_cb));
  bufsize = 131072;
  ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &wf_client, &bufsize));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, wf_write1_done);
  ASSERT_EQ(1, wf_write2_done);
  ASSERT_EQ(NUM_FILLERS + 6, wf_close_cb_called);

  for (i = 0; i < NUM_FILLERS; i++)
    close_socket(wf_filler_fds[i]);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/* --- Test 2: watcher_read_restart_same_poll --- */

static uv_tcp_t rr_server;
static uv_tcp_t rr_clients[2];
static uv_tcp_t rr_peers[2];
static uv_connect_t rr_connect_reqs[2];
static uv_write_t rr_write_reqs[2];
static uv_write_t rr_ping_reqs[2];
static uv_idle_t rr_idle;
static uv_check_t rr_check;
static uv_timer_t rr_watchdog;
static int rr_connected;
static int rr_accepted;
static int rr_phase;
static int rr_victim_idx;
static int rr_victim_restarted;
static int rr_peer_bytes[2];
static int rr_close_cb_called;


static void rr_close_cb(uv_handle_t* handle) {
  rr_close_cb_called++;
}


static void rr_shutdown_test(void) {
  ASSERT_OK(uv_timer_stop(&rr_watchdog));
  ASSERT_OK(uv_check_stop(&rr_check));
  ASSERT_OK(uv_idle_stop(&rr_idle));

  uv_close((uv_handle_t*) &rr_watchdog, rr_close_cb);
  uv_close((uv_handle_t*) &rr_check, rr_close_cb);
  uv_close((uv_handle_t*) &rr_idle, rr_close_cb);
  uv_close((uv_handle_t*) &rr_clients[0], rr_close_cb);
  uv_close((uv_handle_t*) &rr_clients[1], rr_close_cb);
  uv_close((uv_handle_t*) &rr_peers[0], rr_close_cb);
  uv_close((uv_handle_t*) &rr_peers[1], rr_close_cb);
  uv_close((uv_handle_t*) &rr_server, rr_close_cb);
}


static void rr_watchdog_cb(uv_timer_t* handle) {
  FATAL("read stalled after filter deleted in same poll batch");
}


static void rr_idle_cb(uv_idle_t* handle) {
  /* Keeps uv__io_poll timeout at 0 for one tick to flush watcher_queue. */
}


static void rr_write_cb(uv_write_t* req, int status) {
  ASSERT(status == 0 || status == UV_ECANCELED);
}


static void rr_peer_read_cb(uv_stream_t* stream,
                            ssize_t nread,
                            const uv_buf_t* buf) {
  int idx;

  if (nread == 0)
    return;
  ASSERT_GT(nread, 0);

  idx = (stream == (uv_stream_t*) &rr_peers[0]) ? 0 : 1;
  rr_peer_bytes[idx] += (int) nread;

  if (rr_victim_idx == -1) {
    /* Stop reading on the other peer while its EVFILT_READ is also pending in
     * this poll batch. Because that peer still has a pending write (POLLOUT),
     * uv__io_stop(POLLIN) leaves pevents != 0 and kqueue deletes EVFILT_READ
     * when it processes the peer's event later in this same batch.
     */
    rr_victim_idx = 1 - idx;
    ASSERT_OK(uv_read_stop((uv_stream_t*) &rr_peers[rr_victim_idx]));
    return;
  }

  if (rr_peer_bytes[0] == 4 && rr_peer_bytes[1] == 4)
    rr_shutdown_test();
}


static void rr_check_cb(uv_check_t* handle) {
  uv_buf_t ping;

  if (rr_phase == 1) {
    /* Still in the tick where rr_maybe_start_phase1() ran; wait for next poll.
     */
    rr_phase = 2;
    return;
  }

  if (rr_phase == 2) {
    /* watcher_queue has now been flushed: both peers have POLLIN | POLLOUT
     * registered. Send 4 bytes to both peers so both EVFILT_READ filters fire
     * in the same kevent() batch.
     */
    ASSERT_OK(uv_idle_stop(&rr_idle));
    ping = uv_buf_init("ping", 4);
    ASSERT_OK(uv_write(&rr_ping_reqs[0],
                       (uv_stream_t*) &rr_clients[0],
                       &ping,
                       1,
                       rr_write_cb));
    ASSERT_OK(uv_write(&rr_ping_reqs[1],
                       (uv_stream_t*) &rr_clients[1],
                       &ping,
                       1,
                       rr_write_cb));
    /* Give the loopback stack a moment to enqueue both packets before poll. */
    uv_sleep(5);
    rr_phase = 3;
    return;
  }

  if (rr_phase == 3 && rr_victim_idx != -1 && !rr_victim_restarted) {
    /* Restart reading on the victim before the next uv__io_poll() flushes
     * watcher_queue.
     */
    rr_victim_restarted = 1;
    ASSERT_OK(uv_read_start((uv_stream_t*) &rr_peers[rr_victim_idx],
                            alloc_cb,
                            rr_peer_read_cb));
    ASSERT_OK(uv_check_stop(&rr_check));
    if (rr_peer_bytes[0] == 4 && rr_peer_bytes[1] == 4)
      rr_shutdown_test();
  }
}


static void rr_maybe_start_phase1(void) {
  uv_buf_t bufs[64];
  int bufsize;
  int i;
  int j;

  if (rr_connected != 2 || rr_accepted != 2)
    return;

  for (j = 0; j < (int) ARRAY_SIZE(bufs); j++)
    bufs[j] = uv_buf_init(fill_chunk, sizeof(fill_chunk));

  for (i = 0; i < 2; i++) {
    bufsize = 131072;
    ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &rr_peers[i], &bufsize));
    bufsize = 131072;
    ASSERT_OK(uv_recv_buffer_size((uv_handle_t*) &rr_clients[i], &bufsize));

    ASSERT_OK(uv_read_start((uv_stream_t*) &rr_peers[i],
                            alloc_cb,
                            rr_peer_read_cb));

    /* Queue a large blocked write so POLLOUT stays active on both peers and
     * stopping POLLIN leaves pevents != 0.
     */
    ASSERT_OK(uv_write(&rr_write_reqs[i],
                       (uv_stream_t*) &rr_peers[i],
                       bufs,
                       ARRAY_SIZE(bufs),
                       rr_write_cb));
#ifndef _WIN32
    ASSERT_GT(rr_peers[i].write_queue_size, 0);
#endif
  }

  rr_phase = 1;
  ASSERT_OK(uv_idle_start(&rr_idle, rr_idle_cb));
  ASSERT_OK(uv_check_start(&rr_check, rr_check_cb));
}


static void rr_connect_cb(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  rr_connected++;
  rr_maybe_start_phase1();
}


static void rr_connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
  ASSERT_LT(rr_accepted, 2);
  ASSERT_OK(uv_tcp_init(server->loop, &rr_peers[rr_accepted]));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &rr_peers[rr_accepted]));
  rr_accepted++;
  rr_maybe_start_phase1();
}


TEST_IMPL(watcher_read_restart_same_poll) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int i;

  rr_victim_idx = -1;
  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_idle_init(loop, &rr_idle));
  ASSERT_OK(uv_check_init(loop, &rr_check));
  ASSERT_OK(uv_timer_init(loop, &rr_watchdog));
  ASSERT_OK(uv_timer_start(&rr_watchdog, rr_watchdog_cb, 2500, 0));

  ASSERT_OK(uv_tcp_init(loop, &rr_server));
  ASSERT_OK(uv_tcp_bind(&rr_server, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &rr_server, 2, rr_connection_cb));

  for (i = 0; i < 2; i++) {
    ASSERT_OK(uv_tcp_init(loop, &rr_clients[i]));
    ASSERT_OK(uv_tcp_connect(&rr_connect_reqs[i],
                             &rr_clients[i],
                             (const struct sockaddr*) &addr,
                             rr_connect_cb));
  }

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(4, rr_peer_bytes[0]);
  ASSERT_EQ(4, rr_peer_bytes[1]);
  ASSERT_EQ(8, rr_close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
