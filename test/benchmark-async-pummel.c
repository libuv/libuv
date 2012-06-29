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

#include "task.h"
#include "uv.h"

#include <stdio.h>
#include <stdlib.h>

#define NUM_PINGS (1000 * 1000)

static unsigned int callbacks;
static volatile int done;


static void async_cb(uv_async_t* handle, int status) {
  if (++callbacks == NUM_PINGS)
    uv_close((uv_handle_t*) handle, NULL);
}


static void pummel(void* arg) {
  while (!done)
    uv_async_send((uv_async_t*) arg);
}


BENCHMARK_IMPL(async_pummel) {
  uv_async_t handle;
  uv_thread_t tid;
  uint64_t time;

  ASSERT(0 == uv_async_init(uv_default_loop(), &handle, async_cb));
  ASSERT(0 == uv_thread_create(&tid, pummel, &handle));

  time = uv_hrtime();

  ASSERT(0 == uv_run(uv_default_loop()));

  time = uv_hrtime() - time;
  done = 1;

  ASSERT(0 == uv_thread_join(&tid));

  printf("%s callbacks in %.2f seconds (%s/sec)\n",
         fmt(callbacks),
         time / 1e9,
         fmt(callbacks / (time / 1e9)));

  return 0;
}
