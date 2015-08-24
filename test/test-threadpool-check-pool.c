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

/* This number should be default no. of threads in threadpool */
#define N 4

static uv_thread_t tid[N];

static void work_cb(uv_work_t* req) {
  int index = *((int*)(req->data));

  /* Capture which thread did the job */
  tid[index] = uv_thread_self();

  /* The job */
  uv_sleep(1000);
}


TEST_IMPL(threadpool_check_pool) {
  uv_work_t work_req[N];
  int data[N], r, i, j;

  /* Queue N jobs with index as data */
  for(i=0; i<N; i++) {
    data[i] = i;
    work_req[i].data = data + i;
    r = uv_queue_work(uv_default_loop(), (work_req + i), work_cb, NULL);
    ASSERT(r == 0);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  /* N jobs should go to N different threads
   * if a thread has serviced more than 1 job, there was an imbalance */
  for(i = 0; i < (N-1); i++) {
    for(j = (i+1); j < N; j++) {
      ASSERT(uv_thread_equal((tid + i), (tid + j)) == 0);
    }
  }

  MAKE_VALGRIND_HAPPY();
  return 0;
}

