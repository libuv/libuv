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

#include <stdlib.h>
#include <string.h>

#define SEGMENT_SIZE 100
#define NUM_SEGMENTS 4
#define TOTAL_SIZE   (SEGMENT_SIZE * NUM_SEGMENTS)

static uv_udp_t sender;
static uv_udp_t receiver;
static int close_cb_called;
static int recv_cb_called;
static int total_bytes_received;

static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


/*
 * test: batch send without GSO (gso_size=0).
 * Should work on all platforms — just sends N separate datagrams.
 */
TEST_IMPL(udp_gso) {
  struct sockaddr_in addr;
  struct sockaddr_storage bound_addr;
  int bound_len;
  uv_udp_mmsg_t msgs[2];
  uv_buf_t bufs[2];
  unsigned int max_seg;
  int r;

  close_cb_called = 0;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &sender));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&sender, (const struct sockaddr*) &addr, 0));

  bound_len = sizeof(bound_addr);
  ASSERT_OK(uv_udp_getsockname(&sender,
                                 (struct sockaddr*) &bound_addr,
                                 &bound_len));
  ASSERT_OK(uv_udp_connect(&sender,
                              (const struct sockaddr*) &bound_addr));

  max_seg = uv_udp_gso_max_segments(&sender);
  /* 0 = GSO not available (macOS, older Linux), 64 = Linux with GSO. */
  ASSERT(max_seg == 0 || max_seg == 64);

  /* Batch send: two separate messages, no GSO (gso_size=0). */
  bufs[0] = uv_buf_init("AAAA", 4);
  bufs[1] = uv_buf_init("BBBB", 4);
  memset(msgs, 0, sizeof(msgs));
  msgs[0].bufs = &bufs[0];
  msgs[0].nbufs = 1;
  msgs[1].bufs = &bufs[1];
  msgs[1].nbufs = 1;

  r = uv_udp_try_send_batch(&sender, msgs, 2);
  ASSERT_EQ(2, r);

  uv_close((uv_handle_t*) &sender, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/*
 * test: GSO segmentation send with receiver verification.
 *
 * When GSO is available (Linux 4.18+), send a single large buffer with
 * gso_size set. The kernel splits it into segments. Verify the receiver
 * gets the expected number of datagrams with the right sizes.
 *
 * When GSO is not available, verify the batch send falls back to
 * sending the entire buffer as one datagram.
 */
static void gso_alloc_cb(uv_handle_t* handle,
                          size_t suggested_size,
                          uv_buf_t* buf) {
  buf->base = malloc(65536);
  ASSERT_NOT_NULL(buf->base);
  buf->len = 65536;
}


static void gso_recv_cb(uv_udp_t* handle, const uv_udp_recv_t* recv) {
  if (recv->nread == 0) {
    if (recv->buf && recv->buf->base)
      free(recv->buf->base);
    return;
  }

  ASSERT_GT(recv->nread, 0);
  total_bytes_received += recv->nread;
  recv_cb_called++;

  free(recv->buf->base);

  /* Once we've received all expected data, stop. */
  if (total_bytes_received >= TOTAL_SIZE) {
    uv_udp_recv_stop(handle);
    uv_close((uv_handle_t*) &receiver, close_cb);
    uv_close((uv_handle_t*) &sender, close_cb);
  }
}


TEST_IMPL(udp_gso_segments) {
  struct sockaddr_in addr;
  struct sockaddr_storage bound_addr;
  int bound_len;
  uv_udp_mmsg_t msg;
  uv_buf_t buf;
  char* data;
  unsigned int max_seg;
  int r;
  int i;

  close_cb_called = 0;
  recv_cb_called = 0;
  total_bytes_received = 0;

  /* Set up receiver. */
  ASSERT_OK(uv_udp_init(uv_default_loop(), &receiver));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  ASSERT_OK(uv_udp_bind(&receiver, (const struct sockaddr*) &addr, 0));

  bound_len = sizeof(bound_addr);
  ASSERT_OK(uv_udp_getsockname(&receiver,
                                 (struct sockaddr*) &bound_addr,
                                 &bound_len));

  ASSERT_OK(uv_udp_recv_start2(&receiver, gso_alloc_cb, gso_recv_cb));

  /* Set up sender and connect to receiver. */
  ASSERT_OK(uv_udp_init(uv_default_loop(), &sender));
  ASSERT_OK(uv_udp_connect(&sender,
                              (const struct sockaddr*) &bound_addr));

  max_seg = uv_udp_gso_max_segments(&sender);

  /* Build a buffer of NUM_SEGMENTS * SEGMENT_SIZE bytes, each segment
   * filled with a different byte for verification. */
  data = malloc(TOTAL_SIZE);
  ASSERT_NOT_NULL(data);
  for (i = 0; i < NUM_SEGMENTS; i++)
    memset(data + i * SEGMENT_SIZE, 'A' + i, SEGMENT_SIZE);

  buf = uv_buf_init(data, TOTAL_SIZE);
  memset(&msg, 0, sizeof(msg));
  msg.bufs = &buf;
  msg.nbufs = 1;
  msg.gso_size = SEGMENT_SIZE;

  r = uv_udp_try_send_batch(&sender, &msg, 1);
  ASSERT_EQ(1, r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  if (max_seg > 0) {
    /* GSO available: kernel should have split into NUM_SEGMENTS datagrams. */
    ASSERT_EQ(NUM_SEGMENTS, recv_cb_called);
  } else {
    /* GSO not available: entire buffer sent as one datagram. */
    ASSERT_EQ(1, recv_cb_called);
  }
  ASSERT_EQ(TOTAL_SIZE, total_bytes_received);
  ASSERT_EQ(2, close_cb_called);

  free(data);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
