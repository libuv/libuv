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

#if !defined(_WIN32)

#include "uv.h"
#include "task.h"
#include <fcntl.h>
#include <unistd.h>

typedef struct {
  uv_read_t req;
  uv_buf_t buf;
} read_req_t;

static unsigned int read_cb_called;

static char slab[1];

static void read_init(read_req_t** req, char* base, int size)
{
  *req = malloc(sizeof(read_req_t));
  (*req)->buf.base = base;
  (*req)->buf.len = size;
}


static void read_cb(uv_read_t* req, int nread) {
  read_req_t* read_req = (read_req_t*)req;
  uv_stream_t* handle = read_req->req.handle;
  switch (++read_cb_called) {
  case 1:
    ASSERT(nread == 1);
    break;
  case 2:
    ASSERT(nread == UV_EOF);
    free(req);
    uv_close((uv_handle_t *) handle, NULL);
    break;
  default:
    ASSERT(!"read_cb_called > 2");
  }
}

TEST_IMPL(close_fd) {
  uv_pipe_t pipe_handle;
  int fd[2];
  read_req_t* req;

  ASSERT(0 == pipe(fd));
  ASSERT(0 == fcntl(fd[0], F_SETFL, O_NONBLOCK));
  ASSERT(0 == uv_pipe_init(uv_default_loop(), &pipe_handle, 0));
  ASSERT(0 == uv_pipe_open(&pipe_handle, fd[0]));
  fd[0] = -1;  /* uv_pipe_open() takes ownership of the file descriptor. */
  ASSERT(1 == write(fd[1], "", 1));
  ASSERT(0 == close(fd[1]));
  fd[1] = -1;

  read_init(&req, slab, sizeof(slab));
  ASSERT(0 == uv_read(&req->req, (uv_stream_t *) &pipe_handle, &req->buf, 1, read_cb));
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT(1 == read_cb_called);
  ASSERT(0 == uv_is_active((const uv_handle_t *) &pipe_handle));
  ASSERT(0 == uv_read(&req->req, (uv_stream_t *) &pipe_handle, &req->buf, 1, read_cb));
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT(2 == read_cb_called);
  ASSERT(0 != uv_is_closing((const uv_handle_t *) &pipe_handle));

  MAKE_VALGRIND_HAPPY();
  return 0;
}

#endif  /* !defined(_WIN32) */
