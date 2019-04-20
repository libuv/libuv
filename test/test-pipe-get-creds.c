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

static uv_pipe_t pipe_client;
static uv_pipe_t pipe_server;
static uv_pipe_t pipe_remote;
static uv_connect_t connect_req;

static int pipe_close_cb_called = 0;
static int pipe_client_connect_cb_called = 0;


static void pipe_close_cb(uv_handle_t* handle) {
  ASSERT(handle == (uv_handle_t*) &pipe_client ||
         handle == (uv_handle_t*) &pipe_server ||
         handle == (uv_handle_t*) &pipe_remote);
  pipe_close_cb_called++;
}


static void pipe_client_connect_cb(uv_connect_t* req, int status) {
  ASSERT(req == &connect_req);
  ASSERT(status == 0);
  uv_close((uv_handle_t*) &pipe_client, pipe_close_cb);
}


static void pipe_server_connection_cb(uv_stream_t* handle, int status) {
  /* This function *may* be called, depending on whether accept or the
   * connection callback is called first.
   */
  uv_passwd_t passwd;
  uv_pipe_creds_t creds;
  int r;

  ASSERT(status == 0);
  ASSERT(handle == (uv_stream_t *) &pipe_server);

  r = uv_os_get_passwd(&passwd);
  ASSERT(r == 0);

  r = uv_pipe_init(handle->loop, &pipe_remote, 0);
  ASSERT(r == 0);

  r = uv_accept(handle, (uv_stream_t *) &pipe_remote);
  ASSERT(r == 0);

  r = uv_pipe_get_creds(&pipe_remote, &creds);
  ASSERT(r == 0);

#if !defined(BSD) || defined(__APPLE__)
  ASSERT(creds.pid == uv_os_getpid());
#else
  /* BSD has getpid() but can't get the PID of a peer, thus the special case */
  ASSERT(creds.pid == -1);
#endif
  /* euid and egid are -1 on windows */
  ASSERT(creds.euid == passwd.uid);
  ASSERT(creds.egid == passwd.gid);

  pipe_client_connect_cb_called++;

  uv_close((uv_handle_t*) &pipe_remote, pipe_close_cb);
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
}


TEST_IMPL(pipe_get_creds) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  uv_loop_t* loop;
  uv_pipe_creds_t creds;
  int r;

  loop = uv_default_loop();
  ASSERT(loop != NULL);

  r = uv_pipe_init(loop, &pipe_server, 0);
  ASSERT(r == 0);

  r = uv_pipe_get_creds(&pipe_server, &creds);
  ASSERT(r == UV_EBADF);

  r = uv_pipe_bind(&pipe_server, TEST_PIPENAME);
  ASSERT(r == 0);

  r = uv_listen((uv_stream_t*) &pipe_server, 0, pipe_server_connection_cb);
  ASSERT(r == 0);

  r = uv_pipe_init(loop, &pipe_client, 0);
  ASSERT(r == 0);

  r = uv_pipe_get_creds(&pipe_client, &creds);
  ASSERT(r == UV_EBADF);

  uv_pipe_connect(&connect_req, &pipe_client, TEST_PIPENAME, pipe_client_connect_cb);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(r == 0);
  ASSERT(pipe_client_connect_cb_called == 1);
  ASSERT(pipe_close_cb_called == 3);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
