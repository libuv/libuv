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

static uv_pipe_t pipe_dummy;
static uv_connect_t connect_req;
static uv_prepare_t prepare;

static int async_cb_called;
static int close_cb_called;


void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


void connect_cb(uv_connect_t* req, int status) {
  ASSERT(status == UV_ENOENT);
  uv_close((uv_handle_t*) req->handle, close_cb);
}


void async_cb(uv_async_t* handle) {
  async_cb_called++;
  uv_close((uv_handle_t*) handle, close_cb);
}


void prepare_cb(uv_prepare_t* handle) {
  uv_pipe_connect(&connect_req,
                  &pipe_dummy,
                  "nonexistent_file_path",
                  connect_cb);
  uv_close((uv_handle_t*) handle, close_cb);
}


TEST_IMPL(async_multi) {
  uv_loop_t* loop;
  uv_async_t async1;
  uv_async_t async2;

  loop = uv_default_loop();

  ASSERT(0 == uv_async_init(loop, &async1, async_cb));
  ASSERT(0 == uv_async_init(loop, &async2, async_cb));

  /* Create a pending notification */
  ASSERT(0 == uv_pipe_init(loop, &pipe_dummy, 0));
  ASSERT(0 == uv_prepare_init(loop, &prepare));
  ASSERT(0 == uv_prepare_start(&prepare, prepare_cb));

  ASSERT(0 == uv_async_send(&async1));
  ASSERT(0 == uv_async_send(&async2));

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(async_cb_called == 2);
  ASSERT(close_cb_called == 4);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
