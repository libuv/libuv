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
#include <string.h>

static char exepath[1024];
static size_t exepath_size = 1024;
static char* args[3];
static uv_pipe_t channel;
static uv_tcp_t tcp_server;

static int exit_cb_called;
static int read2_cb_called;

static uv_write_t write_req;

static void ipc_on_connection(uv_stream_t* server, int status) {
  ASSERT(status == 0);
  ASSERT((uv_stream_t*)&tcp_server == server);
}


static void exit_cb(uv_process_t* process, int exit_status, int term_signal) {
  printf("exit_cb\n");
  exit_cb_called++;
  ASSERT(exit_status == 1);
  ASSERT(term_signal == 0);
  uv_close((uv_handle_t*)process, NULL);
}


static uv_buf_t on_alloc(uv_handle_t* handle, size_t suggested_size) {
  return uv_buf_init(malloc(suggested_size), suggested_size);
}


static void on_read(uv_pipe_t* pipe, ssize_t nread, uv_buf_t buf,
    uv_handle_type pending) {
  int r;
  uv_buf_t outbuf;

  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf.base);
    return;
  }

  ASSERT(nread > 0 && buf.base && pending != UV_UNKNOWN_HANDLE);
  read2_cb_called++;

  /* Accept the pending TCP server, and start listening on it. */
  ASSERT(pending == UV_TCP);
  r = uv_tcp_init(uv_default_loop(), &tcp_server);
  ASSERT(r == 0);

  r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)&tcp_server);
  ASSERT(r == 0);

  r = uv_listen((uv_stream_t*)&tcp_server, 12, ipc_on_connection);
  ASSERT(r == 0);

  /* Make sure that the expected data is correctly multiplexed. */
  ASSERT(memcmp("hello\n", buf.base, buf.len) == 0);
  fprintf(stderr, "got %d bytes\n", (int)nread);

  outbuf = uv_buf_init("world\n", 6);
  r = uv_write(&write_req, (uv_stream_t*)pipe, &outbuf, 1, NULL);
  ASSERT(r == 0);

  free(buf.base);
}


TEST_IMPL(ipc) {
  int r;
  uv_process_options_t options;
  uv_process_t process;

  r = uv_pipe_init(uv_default_loop(), &channel, 1);
  ASSERT(r == 0);

  memset(&options, 0, sizeof(uv_process_options_t));

  r = uv_exepath(exepath, &exepath_size);
  ASSERT(r == 0);
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = "ipc_helper";
  args[2] = NULL;
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
  options.stdin_stream = &channel;

  r = uv_spawn(uv_default_loop(), &process, options);
  ASSERT(r == 0);

  uv_read2_start((uv_stream_t*)&channel, on_alloc, on_read);

  r = uv_run(uv_default_loop());
  ASSERT(r == 0);

  ASSERT(read2_cb_called == 1);
  ASSERT(exit_cb_called == 1);
  return 0;
}
