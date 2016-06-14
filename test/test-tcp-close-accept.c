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

/* this test is Unix only */
#ifndef _WIN32

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

static struct sockaddr_in addr;
static uv_tcp_t tcp_server;
static uv_tcp_t tcp_outgoing[2];
static uv_tcp_t tcp_incoming[ARRAY_SIZE(tcp_outgoing)];
static uv_connect_t connect_reqs[ARRAY_SIZE(tcp_outgoing)];
static uv_tcp_t tcp_check;
static uv_connect_t tcp_check_req;
static uv_write_t write_reqs[ARRAY_SIZE(tcp_outgoing)];
static unsigned int got_connections;
static unsigned int close_cb_called;
static unsigned int write_cb_called;
static unsigned int read_cb_called;
static unsigned int pending_incoming;

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void write_cb(uv_write_t* req, int status) {
  ASSERT(status == 0);
  write_cb_called++;
}

//连接后 写
static void connect_cb(uv_connect_t* req, int status) {
  unsigned int i;
  uv_buf_t buf;
  uv_stream_t* outgoing;

  if (req == &tcp_check_req) {
    ASSERT(status != 0);

    /*
     * Time to finish the test: close both the check and pending incoming
     * connections
     */
    uv_close((uv_handle_t*) &tcp_incoming[pending_incoming], close_cb);
    uv_close((uv_handle_t*) &tcp_check, close_cb);
    return;
  }

  ASSERT(status == 0);
  ASSERT(connect_reqs <= req);
  ASSERT(req <= connect_reqs + ARRAY_SIZE(connect_reqs));
  i = req - connect_reqs;

  buf = uv_buf_init("x", 1);
  outgoing = (uv_stream_t*) &tcp_outgoing[i];
  ASSERT(0 == uv_write(&write_reqs[i], outgoing, &buf, 1, write_cb));
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char slab[1];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  uv_loop_t* loop;
  unsigned int i;
  pending_incoming = (uv_tcp_t*) stream - &tcp_incoming[0];
  ASSERT(pending_incoming < got_connections);
  ASSERT(0 == uv_read_stop(stream));
  ASSERT(1 == nread);

  loop = stream->loop;
  read_cb_called++;

  /* Close all active incomings, except current one */
  for (i = 0; i < got_connections; i++) {
    if (i != pending_incoming)
      uv_close((uv_handle_t*) &tcp_incoming[i], close_cb);
  }

  /* 创建一个新的请求check */
  ASSERT(0 == uv_tcp_init(loop, &tcp_check));
  ASSERT(0 == uv_tcp_connect(&tcp_check_req,
                             &tcp_check,
                             (const struct sockaddr*) &addr,
                             connect_cb));
  ASSERT(0 == uv_read_start((uv_stream_t*) &tcp_check, alloc_cb, read_cb));

  /* 关闭服务器 */
  uv_close((uv_handle_t*) &tcp_server, close_cb);
}

static void connection_cb(uv_stream_t* server, int status) {
  unsigned int i;
  uv_tcp_t* incoming;

  ASSERT(server == (uv_stream_t*) &tcp_server);

  /* 忽略tcp_check的连接  */
  if (got_connections == ARRAY_SIZE(tcp_incoming))
    return;

  /* 接收连接 */
  incoming = &tcp_incoming[got_connections++];
  ASSERT(0 == uv_tcp_init(server->loop, incoming));
  ASSERT(0 == uv_accept(server, (uv_stream_t*) incoming));

  if (got_connections != ARRAY_SIZE(tcp_incoming))
    return;

  /* 当所有的客户端都连接了开始读取 */
  for (i = 0; i < ARRAY_SIZE(tcp_incoming); i++) {
    incoming = &tcp_incoming[i];
    ASSERT(0 == uv_read_start((uv_stream_t*) incoming, alloc_cb, read_cb));
  }
}

TEST_IMPL(tcp_close_accept) {
  unsigned int i;
  uv_loop_t* loop;
  uv_tcp_t* client;

  /*
   * 下面:
   *
   * 创建服务器并创建两个客户端连接到它 每一个在连接后发送一个字节
   *
   * 当所有客户端都被服务器接收后 - 开始读取第一个客户端的数据并关闭第二个客户端和服务器
   * 之后初始化一个新的连接使用tcp_check handle (thus, reusing fd from second client).
   *
   * uv__io_poll()'s 还会包含第二个客户端的读取请求 
   * 如果没有清除 `tcp_check` 还会收到并调用 `connect_cb` status = 0
   */

  loop = uv_default_loop();
  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT(0 == uv_tcp_init(loop, &tcp_server));
  ASSERT(0 == uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0));
  ASSERT(0 == uv_listen((uv_stream_t*) &tcp_server,
                        ARRAY_SIZE(tcp_outgoing),
                        connection_cb));

  for (i = 0; i < ARRAY_SIZE(tcp_outgoing); i++) {
    client = tcp_outgoing + i;

    ASSERT(0 == uv_tcp_init(loop, client));
    ASSERT(0 == uv_tcp_connect(&connect_reqs[i],
                               client,
                               (const struct sockaddr*) &addr,
                               connect_cb));
  }

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(ARRAY_SIZE(tcp_outgoing) == got_connections);
  ASSERT((ARRAY_SIZE(tcp_outgoing) + 2) == close_cb_called);
  ASSERT(ARRAY_SIZE(tcp_outgoing) == write_cb_called);
  ASSERT(1 == read_cb_called);//读取只有一次

  MAKE_VALGRIND_HAPPY();
  return 0;
}

#endif  /* !_WIN32 */
