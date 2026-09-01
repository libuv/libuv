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

#include <string.h>

#define CHECK_HANDLE(handle) \
  ASSERT_NE((uv_udp_t*)(handle) == &server || (uv_udp_t*)(handle) == &client, 0)

static uv_udp_t server;
static uv_udp_t client;

static int sv_recv_cb_called;

static int close_cb_called;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  CHECK_HANDLE(handle);
  ASSERT_LE(suggested_size, sizeof(slab));
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  ASSERT(uv_is_closing(handle));
  close_cb_called++;
}


static void sv_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       const uv_buf_t* rcvbuf,
                       const struct sockaddr* addr,
                       unsigned flags) {
  if (nread == 0) {
    ASSERT_NULL(addr);
    return;
  }

  ASSERT_EQ(4, nread);
  ASSERT_NOT_NULL(addr);

  if (!memcmp("EXIT", rcvbuf->base, nread)) {
    uv_close((uv_handle_t*) handle, close_cb);
    uv_close((uv_handle_t*) &client, close_cb);
  } else {
    ASSERT_MEM_EQ(rcvbuf->base, "HELO", 4);
  }

  sv_recv_cb_called++;

  if (sv_recv_cb_called == 2)
    uv_udp_recv_stop(handle);
}


TEST_IMPL(udp_try_send) {
  struct sockaddr_in addr;
  static char buffer[64 * 1024];
  uv_buf_t buf;
  uv_buf_t* bufs[] = {&buf, &buf};
  unsigned int nbufs[] = {1, 1};
  struct sockaddr* addrs[] = {
    (struct sockaddr*) &addr,
    (struct sockaddr*) &addr,
  };

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_udp_init(uv_default_loop(), &server));
  ASSERT_OK(uv_udp_bind(&server, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_udp_recv_start(&server, alloc_cb, sv_recv_cb));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));

  buf = uv_buf_init(buffer, sizeof(buffer));

  ASSERT_EQ(uv_udp_try_send(&client, &buf, 0, (const struct sockaddr*) &addr),
            UV_EINVAL);

  ASSERT_EQ(uv_udp_try_send(&client, &buf, 1, (const struct sockaddr*) &addr),
            UV_EMSGSIZE);

  ASSERT_EQ(0, sv_recv_cb_called);

  buf = uv_buf_init("HELO", 4);
  ASSERT_EQ(2, uv_udp_try_send2(&client, 2, bufs, nbufs, addrs, /*flags*/0));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(2, sv_recv_cb_called);

  ASSERT_OK(uv_udp_recv_start(&server, alloc_cb, sv_recv_cb));

  buf = uv_buf_init("EXIT", 4);
  ASSERT_EQ(uv_udp_try_send(&client, &buf, 1, (const struct sockaddr*) &addr),
            4);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(3, sv_recv_cb_called);

  ASSERT_OK(client.send_queue_size);
  ASSERT_OK(server.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#define OVER_BATCH_COUNT 45
static int over_batch_seen[OVER_BATCH_COUNT];
static int over_batch_recv_cb_called;
static char over_batch_bufs_storage[OVER_BATCH_COUNT][4];


static void over_batch_recv_cb(uv_udp_t* handle,
                                ssize_t nread,
                                const uv_buf_t* rcvbuf,
                                const struct sockaddr* addr,
                                unsigned flags) {
  unsigned int idx;

  if (nread == 0) {
    ASSERT_NULL(addr);
    return;
  }

  ASSERT_EQ(4, nread);
  ASSERT_NOT_NULL(addr);

  idx = (unsigned char) rcvbuf->base[2] * 10 + (unsigned char) rcvbuf->base[3];
  ASSERT_LT(idx, OVER_BATCH_COUNT);
  over_batch_seen[idx]++;
  over_batch_recv_cb_called++;

  if (over_batch_recv_cb_called == OVER_BATCH_COUNT)
    uv_udp_recv_stop(handle);
}


TEST_IMPL(udp_try_send2_over_batch_size) {
  struct sockaddr_in addr;
  uv_buf_t* bufs[OVER_BATCH_COUNT];
  unsigned int nbufs[OVER_BATCH_COUNT];
  struct sockaddr* addrs[OVER_BATCH_COUNT];
  uv_buf_t buf_storage[OVER_BATCH_COUNT];
  unsigned int i;
  int r;

  /* uv__udp_sendmsgv() batches datagrams in groups of 20 (ARRAY_SIZE(m) in
   * src/unix/udp.c). Sending more than that in a single try_send2() call
   * used to corrupt the loop's own bookkeeping: some datagrams past the
   * first batch were skipped entirely while others got sent twice.
   */
  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_udp_init(uv_default_loop(), &server));
  ASSERT_OK(uv_udp_bind(&server, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_udp_recv_start(&server, alloc_cb, over_batch_recv_cb));
  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &addr));
  ASSERT_OK(uv_udp_bind(&client, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  for (i = 0; i < OVER_BATCH_COUNT; i++) {
    over_batch_bufs_storage[i][0] = 'P';
    over_batch_bufs_storage[i][1] = 'K';
    over_batch_bufs_storage[i][2] = (char) (i / 10);
    over_batch_bufs_storage[i][3] = (char) (i % 10);
    buf_storage[i] = uv_buf_init(over_batch_bufs_storage[i], 4);
    bufs[i] = &buf_storage[i];
    nbufs[i] = 1;
    addrs[i] = (struct sockaddr*) &addr;
  }

  r = uv_udp_try_send2(&client, OVER_BATCH_COUNT, bufs, nbufs, addrs, 0);
  ASSERT_EQ(r, OVER_BATCH_COUNT);

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(OVER_BATCH_COUNT, over_batch_recv_cb_called);
  for (i = 0; i < OVER_BATCH_COUNT; i++)
    ASSERT_EQ(1, over_batch_seen[i]);

  uv_close((uv_handle_t*) &server, close_cb);
  uv_close((uv_handle_t*) &client, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
