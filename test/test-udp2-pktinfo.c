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

static uv_udp2_t sender;
static uv_udp2_t receiver;

static int recv_cb_called;
static int send_cb_called;
static int close_cb_called;

static struct sockaddr_in receiver_addr;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void alloc_cb(uv_udp2_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void recv_cb(uv_udp2_t* handle, const uv_udp2_recv_t* recv) {
  const struct sockaddr_in* local4;

  if (recv->nread == 0)
    return;

  ASSERT_GT(recv->nread, 0);
  ASSERT_NOT_NULL(recv->addr);
  ASSERT_EQ(4, recv->nread);
  ASSERT(!memcmp("PING", recv->buf->base, 4));

  if (recv->local.ss_family == AF_INET) {
    local4 = (const struct sockaddr_in*) &recv->local;
    ASSERT_EQ(local4->sin_addr.s_addr, htonl(INADDR_LOOPBACK));
  }

  recv_cb_called++;

  uv_udp2_recv_stop(handle);
  uv_close((uv_handle_t*) &receiver, close_cb);
  uv_close((uv_handle_t*) &sender, close_cb);
}


static void send_cb(uv_udp2_send_t* req, int status) {
  ASSERT_OK(status);
  send_cb_called++;
}


TEST_IMPL(udp2_pktinfo) {
  struct sockaddr_in bind_addr;
  uv_udp2_send_t send_req;
  uv_buf_t buf;
  int namelen;
  int r;

  /* Init receiver with RECVPKTINFO. */
  r = uv_udp2_init(uv_default_loop(), &receiver);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &bind_addr));
  r = uv_udp2_bind(&receiver,
                    (const struct sockaddr*) &bind_addr,
                    UV_UDP2_RECVPKTINFO);
  ASSERT_OK(r);

  /* Get the port the OS assigned. */
  namelen = sizeof(receiver_addr);
  r = uv_udp2_getsockname(&receiver,
                           (struct sockaddr*) &receiver_addr,
                           &namelen);
  ASSERT_OK(r);

  r = uv_udp2_recv_start(&receiver, alloc_cb, recv_cb);
  ASSERT_OK(r);

  /* Init sender. */
  r = uv_udp2_init(uv_default_loop(), &sender);
  ASSERT_OK(r);

  /* Send a datagram to the receiver. */
  buf = uv_buf_init("PING", 4);
  r = uv_udp2_send(&send_req,
                    &sender,
                    &buf,
                    1,
                    (const struct sockaddr*) &receiver_addr,
                    send_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, recv_cb_called);
  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
