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
 * Verify that UDP send errors (including ENOBUFS) complete with a callback
 * rather than silently retaining the request on the write queue.
 *
 * Before the fix, ENOBUFS was mapped to UV_EAGAIN, causing the request to
 * stay on write_queue with POLLOUT armed.  On BSD, ENOBUFS from interface
 * TX queue overflow leaves the socket writable, so POLLOUT fires immediately
 * and the event loop spins at 100% CPU with zero callbacks.
 *
 * This test floods UDP sends with a tiny send buffer.  All send callbacks
 * must eventually fire — with either success (0) or an error status — and
 * the event loop must terminate.  A 5-second watchdog timer catches spins.
 */

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uv_udp_t handle_;
static uv_timer_t watchdog_;

#define NUM_SENDS 1000
static uv_udp_send_t reqs_[NUM_SENDS];

static int send_cb_called;
static int send_cb_enobufs;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void send_cb(uv_udp_send_t* req, int status) {
  send_cb_called++;
  if (status == UV_ENOBUFS)
    send_cb_enobufs++;

  /* Once all callbacks have fired, stop the watchdog and close. */
  if (send_cb_called == NUM_SENDS) {
    uv_timer_stop(&watchdog_);
    uv_close((uv_handle_t*) &watchdog_, close_cb);
    uv_close((uv_handle_t*) &handle_, close_cb);
  }
}


static void watchdog_cb(uv_timer_t* timer) {
  /* If we get here, the event loop has been running for 5 seconds without
   * all callbacks firing — likely a spin.  Fail the test. */
  fprintf(stderr,
          "WATCHDOG: only %d/%d callbacks after 5s (enobufs=%d)\n",
          send_cb_called,
          NUM_SENDS,
          send_cb_enobufs);
  ASSERT(0 && "watchdog fired — event loop likely spinning on ENOBUFS");
}


TEST_IMPL(udp_send_enobufs) {
  struct sockaddr_in addr;
  char payload[64];
  uv_buf_t buf;
  int sndbuf;
  int r;
  int i;

  memset(payload, 'X', sizeof(payload));
  buf = uv_buf_init(payload, sizeof(payload));

  /* Send to a non-listening port on localhost. */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &handle_);
  ASSERT_OK(r);

  /* Shrink the send buffer to increase the chance of hitting ENOBUFS. */
  sndbuf = 1;
  r = uv_send_buffer_size((uv_handle_t*) &handle_, &sndbuf);
  ASSERT_OK(r);

  /* Watchdog: if the loop spins for 5s, the test fails. */
  r = uv_timer_init(uv_default_loop(), &watchdog_);
  ASSERT_OK(r);
  r = uv_timer_start(&watchdog_, watchdog_cb, 5000, 0);
  ASSERT_OK(r);

  /* Fire a burst of sends. */
  for (i = 0; i < NUM_SENDS; i++) {
    r = uv_udp_send(&reqs_[i],
                     &handle_,
                     &buf,
                     1,
                     (const struct sockaddr*) &addr,
                     send_cb);
    ASSERT_OK(r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  /* Every send must have completed with a callback. */
  ASSERT_EQ(send_cb_called, NUM_SENDS);
  ASSERT_EQ(close_cb_called, 2);

  fprintf(stderr,
          "info: %d/%d sends, %d ENOBUFS callbacks\n",
          send_cb_called,
          NUM_SENDS,
          send_cb_enobufs);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
