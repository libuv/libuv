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

/*
 * GRO (Generic Receive Offload) test.
 *
 * On Linux 5.0+, the kernel can coalesce consecutive UDP datagrams from the
 * same source into a single "super-packet". When UV_UDP_GRO is set, libuv
 * splits these back into individual datagrams, setting recv_t.segment_size
 * to the stride.
 *
 * On non-Linux or older kernels, UV_UDP_GRO is silently ignored. The test
 * is structured to pass either way: it verifies that all sent data is
 * received correctly, and on Linux checks the GRO-specific metadata.
 */

#define DGRAM_SIZE  200
#define NUM_DGRAMS  5
#define TOTAL_BYTES (DGRAM_SIZE * NUM_DGRAMS)

static uv_udp_t sender;
static uv_udp_t receiver;
static int close_cb_called;
static int recv_cb_called;
static int total_bytes_received;


static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  /* Allocate a large buffer so the kernel can deliver coalesced packets. */
  buf->base = malloc(65536);
  ASSERT_NOT_NULL(buf->base);
  buf->len = 65536;
}


static void recv_cb(uv_udp_t* handle, const uv_udp_recv_t* recv) {
  if (recv->nread == 0) {
    if (recv->buf && recv->buf->base)
      free(recv->buf->base);
    return;
  }

  ASSERT_GT(recv->nread, 0);
  ASSERT_NOT_NULL(recv->addr);

  total_bytes_received += recv->nread;
  recv_cb_called++;

  /* If segment_size is set, libuv split a GRO super-packet for us.
   * Each callback should get exactly one segment's worth of data. */
  if (recv->segment_size > 0)
    ASSERT_EQ((unsigned int) recv->nread, recv->segment_size);

  free(recv->buf->base);

  if (total_bytes_received >= TOTAL_BYTES) {
    uv_udp_recv_stop(handle);
    uv_close((uv_handle_t*) &receiver, close_cb);
    uv_close((uv_handle_t*) &sender, close_cb);
  }
}


TEST_IMPL(udp_gro) {
  struct sockaddr_in addr;
  struct sockaddr_storage bound_addr;
  int bound_len;
  uv_buf_t buf;
  char data[DGRAM_SIZE];
  int r;
  int i;

  close_cb_called = 0;
  recv_cb_called = 0;
  total_bytes_received = 0;

  /* Set up receiver with GRO enabled. */
  ASSERT_OK(uv_udp_init(uv_default_loop(), &receiver));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &addr));
  r = uv_udp_bind(&receiver,
                    (const struct sockaddr*) &addr,
                    UV_UDP_GRO);
  ASSERT_OK(r);

  bound_len = sizeof(bound_addr);
  ASSERT_OK(uv_udp_getsockname(&receiver,
                                 (struct sockaddr*) &bound_addr,
                                 &bound_len));

  ASSERT_OK(uv_udp_recv_start2(&receiver, alloc_cb, recv_cb));

  /* Set up sender. */
  ASSERT_OK(uv_udp_init(uv_default_loop(), &sender));

  /* Send NUM_DGRAMS identical-size datagrams. On Linux with GRO, the kernel
   * may coalesce some or all of them. libuv should split them back. */
  memset(data, 'X', sizeof(data));
  buf = uv_buf_init(data, DGRAM_SIZE);

  for (i = 0; i < NUM_DGRAMS; i++) {
    r = uv_udp_try_send(&sender,
                          &buf,
                          1,
                          (const struct sockaddr*) &bound_addr);
    ASSERT_EQ(DGRAM_SIZE, r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  /* Whether or not GRO coalesced, all data must arrive. */
  ASSERT_EQ(TOTAL_BYTES, total_bytes_received);

  /* Without GRO (or if the kernel didn't coalesce), we get one callback
   * per datagram. With GRO + libuv splitting, we also get one callback
   * per segment. Either way: at least NUM_DGRAMS callbacks. */
  ASSERT_GE(recv_cb_called, NUM_DGRAMS);

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
