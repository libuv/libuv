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
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
# include <unistd.h>
#endif


static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;

static uv_connect_t connect_req;
static uv_write_t write_req;


static void close_socket(uv_tcp_t* sock) {
  uv_os_fd_t fd;
  int r;

  r = uv_fileno((uv_handle_t*)sock, &fd);
  ASSERT(r == 0);
#ifdef _WIN32
  r = closesocket(fd);
#else
  r = close(fd);
#endif
  ASSERT(r == 0);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT(handle != NULL);
  close_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT(req != NULL);

  ASSERT(status != 0);
  fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
  write_cb_called++;

  uv_close((uv_handle_t*)(req->handle), close_cb);
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  uv_stream_t* stream;
  int r;

  ASSERT(req == &connect_req);
  ASSERT(status == 0);

  stream = req->handle;
  connect_cb_called++;

  /* close the socket, the hard way */
  close_socket((uv_tcp_t*)stream);

  buf = uv_buf_init("hello\n", 6);
  r = uv_write(&write_req, stream, &buf, 1, write_cb);
  ASSERT(r == 0);
}


TEST_IMPL(tcp_write_fail) {
  struct sockaddr_in addr;
  uv_tcp_t client;
  int r;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT(r == 0);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT(r == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(connect_cb_called == 1);
  ASSERT(write_cb_called == 1);
  ASSERT(close_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
