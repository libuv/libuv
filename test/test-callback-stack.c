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

/*
 * TODO: Add explanation of why we want on_close to be called from fresh
 * stack.
 */

#include "../oio.h"
#include "task.h"


int nested = 0;
int close_cb_called = 0;


void close_cb(oio_handle *handle, int err) {
  ASSERT(!err);
  ASSERT(nested == 0 && "oio_close_cb must be called from a fresh stack");
  close_cb_called++;
}


TEST_IMPL(close_cb_stack) {
  oio_handle handle;

  oio_init();

  if (oio_tcp_init(&handle, &close_cb, NULL)) {
    FATAL("oio_tcp_init failed");
  }

  nested++;

  if (oio_close(&handle)) {
    FATAL("oio_close failed");
  }

  nested--;

  oio_run();

  ASSERT(nested == 0);
  ASSERT(close_cb_called == 1 && "oio_close_cb must be called exactly once");

  return 0;
}
