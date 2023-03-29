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

#define RECURS_SIZE 1000

static int work_cb_count;
static int work_cb_count2;
static int after_work_cb_count;
static uv_work_t work_req;
static char data;


static void work_cb(uv_work_t* req) {
  ASSERT(req == &work_req);
  ASSERT(req->data == &data);
  work_cb_count++;
}


static void after_work_cb(uv_work_t* req, int status) {
  ASSERT(status == 0);
  ASSERT(req == &work_req);
  ASSERT(req->data == &data);
  after_work_cb_count++;
}


TEST_IMPL(threadpool_queue_work_simple) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, work_cb, after_work_cb);
  ASSERT(r == 0);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(work_cb_count == 1);
  ASSERT(after_work_cb_count == 1);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(threadpool_queue_work_einval) {
  int r;

  work_req.data = &data;
  r = uv_queue_work(uv_default_loop(), &work_req, NULL, after_work_cb);
  ASSERT(r == UV_EINVAL);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(work_cb_count == 0);
  ASSERT(after_work_cb_count == 0);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static uv_work_t* create_work_inst(void) {
  uv_work_t* req = malloc(sizeof(*req));
  ASSERT_NOT_NULL(req);
  return req;
}


/* This is necessary since uv_queue_work() requires the call to uv_after_work_cb
 * to be called, even in the case the req was cancelled. Only then is it removed
 * from the internal queue and able to be free'd by the user. */
static void after_work_recurs_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  free(req);
  after_work_cb_count++;
}


static void req_work(uv_loop_t* loop, uv_work_cb cb) {
  int r;
  uv_work_t* req2 = create_work_inst();
  r = uv_queue_work(loop, req2, cb, after_work_recurs_cb);
  ASSERT_OK(r);
}


static void work_recurs_cb(uv_work_t* req) {
  if (++work_cb_count >= RECURS_SIZE)
    return;
  req_work(req->loop, work_recurs_cb);
}


static void work_recurs2_cb(uv_work_t* req) {
  if (++work_cb_count2 >= RECURS_SIZE)
    return;
  req_work(req->loop, work_recurs2_cb);
}


TEST_IMPL(threadpool_queue_recursive) {
  uv_work_t* req1;
  uv_work_t* req2;
  int r;

  req1 = create_work_inst();
  req2 = create_work_inst();

  r = uv_queue_work(uv_default_loop(),
                    req1,
                    work_recurs_cb,
                    after_work_recurs_cb);
  ASSERT_OK(r);
  r = uv_queue_work(uv_default_loop(),
                    req2,
                    work_recurs2_cb,
                    after_work_recurs_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(work_cb_count, RECURS_SIZE);
  ASSERT_EQ(work_cb_count2, RECURS_SIZE);
  ASSERT_EQ(after_work_cb_count, RECURS_SIZE * 2);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
