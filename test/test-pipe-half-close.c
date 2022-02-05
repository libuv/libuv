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
#include <errno.h>

/* Expected sequence:
 * # Output from process `pipe_half_close`:
 * # Loop count: 1
 * #   Listen CB with status 0
 * #   Client connect CB with status 0
 * #   Client write CB with status 0
 * #   Client shutdown CB with status 0
 * # Loop count: 2
 * #   Read 33 from server.
 * # Loop count: 3
 * #   Read -4095 from server.
 * #   Got EOF
 * #   Server write CB with status 0
 * # Loop count: 4
 * #   Read 39 from client.
 * #   Client close CB
 * # Loop count: 5
 * #   Read -32 from server.
 * #   Listen close CB
 * #   Server close CB
 */

/* Original broken sequence:
 * # Output from process `pipe_half_close`:
 * # Loop count: 1
 * #   Listen CB with status 0
 * #   Client connect CB with status 0
 * #   Client write CB with status 0
 * #   Client shutdown CB with status 0
 * # Loop count: 2
 * #   Read 33 from server.
 * # Loop count: 3
 * #   Read -4095 from server.
 * #   Got EOF
 * #   Server write CB with status 0
 * # Loop count: 4
 * #   Read 39 from client.
 * #   Client close CB
 * # Loop count: 5
 * # Loop count: 6
 * # Assertion failed in .../test-pipe-half-close.c on line 175: loop_cnt <= 5
 */

static uv_pipe_t pipe_client;
static uv_pipe_t pipe_listen;
static uv_pipe_t pipe_server;
static uv_connect_t connect_req;
static uv_write_t write_req;
static uv_shutdown_t shutdown_req;
static uv_idle_t idle_handle;
static uv_buf_t start_write_buf[] = {
  {.base = "{\"start_job\": \"some job details\"}", .len=33}
};
static uv_buf_t result_write_buf[] = {
  {.base = "{\"result\": \"In progress, ETA 3 hours.\"}", .len=39}
};
static unsigned int loop_cnt = 0;


static int pipe_server_read_cb_called = 0;

static void malloc_cb(uv_handle_t* handle,
                      size_t suggested_size,
                      uv_buf_t* buf) {
  buf->len = 0;
  buf->base = malloc(suggested_size);
  if (buf->base != NULL) {
    buf->len = suggested_size;
  }
}

static void pipe_client_write_cb(uv_write_t* req, int status) {
  fprintf(stderr, "  Client write CB with status %d\n", status);
}

static void pipe_server_write_cb(uv_write_t* req, int status) {
  fprintf(stderr, "  Server write CB with status %d\n", status);
}

static void pipe_client_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "  Client close CB\n");
}

static void pipe_server_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "  Server close CB\n");
}

static void pipe_listen_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "  Listen close CB\n");
}

static void pipe_client_shutdown_cb(uv_shutdown_t* req, int status) {
  fprintf(stderr, "  Client shutdown CB with status %d\n", status);
  ASSERT(status == 0);
}

static void pipe_client_connect_cb(uv_connect_t* req, int status) {
  fprintf(stderr, "  Client connect CB with status %d\n", status);
  ASSERT(status == 0);

  uv_write(&write_req,
           (uv_stream_t*)&pipe_client,
           start_write_buf,
           1,
           pipe_client_write_cb);
  uv_shutdown(&shutdown_req,
              (uv_stream_t*)&pipe_client,
              pipe_client_shutdown_cb);
}

static void pipe_server_read_cb(uv_stream_t* stream,
                               ssize_t nread,
                               const uv_buf_t* buf) {
  fprintf(stderr, "  Read %ld from server.\n", nread);
  if (nread > 0) {
    ASSERT(memcmp(start_write_buf->base, buf->base, nread) == 0);
  }
  else if (nread == UV_EOF) {
    ASSERT(pipe_server_read_cb_called == 1);
    fprintf(stderr, "  Got EOF\n");
    uv_write(&write_req,
             (uv_stream_t*)&pipe_server,
             result_write_buf,
             1,
             pipe_server_write_cb);
  }
  else if (nread == UV_EPIPE) {
    ASSERT(pipe_server_read_cb_called == 2);
    uv_close((uv_handle_t*)&pipe_server, pipe_server_close_cb);
    uv_close((uv_handle_t*)&pipe_listen, pipe_listen_close_cb);
    uv_idle_stop(&idle_handle);
  }
  else {
    ASSERT(0);
  }
  pipe_server_read_cb_called++;
}

static void pipe_listen_connection_cb(uv_stream_t* handle, int status) {
  fprintf(stderr, "  Listen CB with status %d\n", status);
  ASSERT(status == 0);
  int r;
  r = uv_pipe_init(handle->loop, &pipe_server, 0);
  ASSERT(r == 0);
  r = uv_accept(handle, (uv_stream_t*)&pipe_server);
  ASSERT(r == 0);
  uv_read_start((uv_stream_t*)&pipe_server, malloc_cb, pipe_server_read_cb);
  uv_read_err_enable((uv_stream_t*)&pipe_server);
}

static void pipe_client_read_cb(uv_stream_t* stream,
                               ssize_t nread,
                               const uv_buf_t* buf) {
  fprintf(stderr, "  Read %ld from client.\n", nread);
  ASSERT(nread > 0);
  ASSERT((size_t)nread == result_write_buf->len);
  ASSERT(memcmp(result_write_buf->base, buf->base, nread) == 0);
  uv_close((uv_handle_t*)&pipe_client, pipe_client_close_cb);
}

static void idle_cb(uv_idle_t* handle) {
  fprintf(stderr, "Loop count: %u\n", ++loop_cnt);
  ASSERT(loop_cnt <= 5);
}

TEST_IMPL(pipe_half_close) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_idle_init(loop, &idle_handle);
  ASSERT(r == 0);
  uv_idle_start(&idle_handle, idle_cb);

  r = uv_pipe_init(loop, &pipe_listen, 0);
  ASSERT(r == 0);

  r = uv_pipe_bind(&pipe_listen, TEST_PIPENAME);
  ASSERT(r == 0);

  r = uv_listen((uv_stream_t*) &pipe_listen, 0, pipe_listen_connection_cb);
  ASSERT(r == 0);

  r = uv_pipe_init(loop, &pipe_client, 0);
  ASSERT(r == 0);

  uv_pipe_connect(&connect_req,
                 &pipe_client,
                 TEST_PIPENAME,
                 pipe_client_connect_cb);
  uv_read_start((uv_stream_t*)&pipe_client, malloc_cb, pipe_client_read_cb);
  uv_read_err_enable((uv_stream_t*)&pipe_client);

  r = uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
