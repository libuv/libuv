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

#include <stdlib.h>
#include <string.h>

#define INIT_CANCEL_INFO(ci, what)                                            \
  do {                                                                        \
    (ci)->reqs = (what);                                                      \
    (ci)->nreqs = ARRAY_SIZE(what);                                           \
    (ci)->stride = sizeof((what)[0]);                                         \
  }                                                                           \
  while (0)

struct cancel_info {
  void* reqs;
  unsigned nreqs;
  unsigned stride;
  uv_timer_t timer_handle;
};

static uv_cond_t signal_cond;
static uv_mutex_t signal_mutex;
static uv_mutex_t wait_mutex;
static unsigned num_threads;
static unsigned fs_cb_called;
static unsigned work_cb_called;
static unsigned done_cb_called;
static unsigned done2_cb_called;
static unsigned timer_cb_called;


static int no_threadpool(void) {
  const char* val = getenv("UV_THREADPOOL_SIZE");
  return val != NULL && strcmp(val, "really-zero") == 0;
}


static void work_cb(uv_work_t* req) {
  uv_mutex_lock(&signal_mutex);
  uv_cond_signal(&signal_cond);
  uv_mutex_unlock(&signal_mutex);

  uv_mutex_lock(&wait_mutex);
  uv_mutex_unlock(&wait_mutex);

  work_cb_called++;
}


static void done_cb(uv_work_t* req, int status) {
  done_cb_called++;
  free(req);
}


static void saturate_threadpool(void) {
  uv_work_t* req;

  ASSERT(0 == uv_cond_init(&signal_cond));
  ASSERT(0 == uv_mutex_init(&signal_mutex));
  ASSERT(0 == uv_mutex_init(&wait_mutex));

  uv_mutex_lock(&signal_mutex);
  uv_mutex_lock(&wait_mutex);

  for (num_threads = 0; /* empty */; num_threads++) {
    req = malloc(sizeof(*req));
    ASSERT(req != NULL);
    ASSERT(0 == uv_queue_work(uv_default_loop(), req, work_cb, done_cb));

    /* Expect to get signalled within 350 ms, otherwise assume that
     * the thread pool is saturated. As with any timing dependent test,
     * this is obviously not ideal.
     */
    if (uv_cond_timedwait(&signal_cond,
                          &signal_mutex,
                          (uint64_t) (350 * 1e6))) {
      ASSERT(0 == uv_cancel((uv_req_t*) req));
      break;
    }
  }
}


static void unblock_threadpool(void) {
  uv_mutex_unlock(&signal_mutex);
  uv_mutex_unlock(&wait_mutex);
}


static void cleanup_threadpool(void) {
  ASSERT(done_cb_called == num_threads + 1);  /* +1 == cancelled work req. */
  ASSERT(work_cb_called == num_threads);

  uv_cond_destroy(&signal_cond);
  uv_mutex_destroy(&signal_mutex);
  uv_mutex_destroy(&wait_mutex);
}


static void fs_cb(uv_fs_t* req) {
  ASSERT(req->result == UV_ECANCELED);
  uv_fs_req_cleanup(req);
  fs_cb_called++;
}


static void getaddrinfo_cb(uv_getaddrinfo_t* req,
                           int status,
                           struct addrinfo* res) {
  ASSERT(status == UV_EAI_CANCELED);
  ASSERT(res == NULL);
  uv_freeaddrinfo(res);  /* Should not crash. */
}


static void getnameinfo_cb(uv_getnameinfo_t* handle,
                           int status,
                           const char* hostname,
                           const char* service) {
  ASSERT(status == UV_EAI_CANCELED);
  ASSERT(hostname == NULL);
  ASSERT(service == NULL);
}


static void work2_cb(uv_work_t* req) {
  ASSERT(0 && "work2_cb called");
}


static void done2_cb(uv_work_t* req, int status) {
  ASSERT(status == UV_ECANCELED);
  done2_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  struct cancel_info* ci;
  uv_req_t* req;
  unsigned i;

  ci = container_of(handle, struct cancel_info, timer_handle);

  for (i = 0; i < ci->nreqs; i++) {
    req = (uv_req_t*) ((char*) ci->reqs + i * ci->stride);
    ASSERT(0 == uv_cancel(req));
  }

  uv_close((uv_handle_t*) &ci->timer_handle, NULL);
  unblock_threadpool();
  timer_cb_called++;
}


static void nop_work_cb(uv_work_t* req) {
}


static void nop_done_cb(uv_work_t* req, int status) {
  req->data = "OK";
}


TEST_IMPL(threadpool_cancel_getaddrinfo) {
  uv_getaddrinfo_t reqs[4];
  struct cancel_info ci;
  struct addrinfo hints;
  uv_loop_t* loop;
  int r;

  /* Work requests are executed synchronously when there is no threadpool. */
  if (no_threadpool())
    RETURN_SKIP("Cancellation doesn't work in synchronous work mode.");

  INIT_CANCEL_INFO(&ci, reqs);
  loop = uv_default_loop();
  saturate_threadpool();

  r = uv_getaddrinfo(loop, reqs + 0, getaddrinfo_cb, "fail", NULL, NULL);
  ASSERT(r == 0);

  r = uv_getaddrinfo(loop, reqs + 1, getaddrinfo_cb, NULL, "fail", NULL);
  ASSERT(r == 0);

  r = uv_getaddrinfo(loop, reqs + 2, getaddrinfo_cb, "fail", "fail", NULL);
  ASSERT(r == 0);

  r = uv_getaddrinfo(loop, reqs + 3, getaddrinfo_cb, "fail", NULL, &hints);
  ASSERT(r == 0);

  ASSERT(0 == uv_timer_init(loop, &ci.timer_handle));
  ASSERT(0 == uv_timer_start(&ci.timer_handle, timer_cb, 10, 0));
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  ASSERT(1 == timer_cb_called);

  cleanup_threadpool();

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(threadpool_cancel_getnameinfo) {
  uv_getnameinfo_t reqs[4];
  struct sockaddr_in addr4;
  struct cancel_info ci;
  uv_loop_t* loop;
  int r;

  /* Work requests are executed synchronously when there is no threadpool. */
  if (no_threadpool())
    RETURN_SKIP("Cancellation doesn't work in synchronous work mode.");

  r = uv_ip4_addr("127.0.0.1", 80, &addr4);
  ASSERT(r == 0);

  INIT_CANCEL_INFO(&ci, reqs);
  loop = uv_default_loop();
  saturate_threadpool();

  r = uv_getnameinfo(loop, reqs + 0, getnameinfo_cb, (const struct sockaddr*)&addr4, 0);
  ASSERT(r == 0);

  r = uv_getnameinfo(loop, reqs + 1, getnameinfo_cb, (const struct sockaddr*)&addr4, 0);
  ASSERT(r == 0);

  r = uv_getnameinfo(loop, reqs + 2, getnameinfo_cb, (const struct sockaddr*)&addr4, 0);
  ASSERT(r == 0);

  r = uv_getnameinfo(loop, reqs + 3, getnameinfo_cb, (const struct sockaddr*)&addr4, 0);
  ASSERT(r == 0);

  ASSERT(0 == uv_timer_init(loop, &ci.timer_handle));
  ASSERT(0 == uv_timer_start(&ci.timer_handle, timer_cb, 10, 0));
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  ASSERT(1 == timer_cb_called);

  cleanup_threadpool();

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(threadpool_cancel_work) {
  struct cancel_info ci;
  uv_work_t reqs[16];
  uv_loop_t* loop;
  unsigned i;

  /* Work requests are executed synchronously when there is no threadpool. */
  if (no_threadpool())
    RETURN_SKIP("Cancellation doesn't work in synchronous work mode.");

  INIT_CANCEL_INFO(&ci, reqs);
  loop = uv_default_loop();
  saturate_threadpool();

  for (i = 0; i < ARRAY_SIZE(reqs); i++)
    ASSERT(0 == uv_queue_work(loop, reqs + i, work2_cb, done2_cb));

  ASSERT(0 == uv_timer_init(loop, &ci.timer_handle));
  ASSERT(0 == uv_timer_start(&ci.timer_handle, timer_cb, 10, 0));
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  ASSERT(1 == timer_cb_called);
  ASSERT(ARRAY_SIZE(reqs) == done2_cb_called);

  cleanup_threadpool();

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(threadpool_cancel_fs) {
  struct cancel_info ci;
  uv_fs_t reqs[25];
  uv_loop_t* loop;
  unsigned n;

  /* Work requests are executed synchronously when there is no threadpool. */
  if (no_threadpool())
    RETURN_SKIP("Cancellation doesn't work in synchronous work mode.");

  INIT_CANCEL_INFO(&ci, reqs);
  loop = uv_default_loop();
  saturate_threadpool();

  /* Needs to match ARRAY_SIZE(fs_reqs). */
  n = 0;
  ASSERT(0 == uv_fs_chmod(loop, reqs + n++, "/", 0, fs_cb));
  ASSERT(0 == uv_fs_chown(loop, reqs + n++, "/", 0, 0, fs_cb));
  ASSERT(0 == uv_fs_close(loop, reqs + n++, 0, fs_cb));
  ASSERT(0 == uv_fs_fchmod(loop, reqs + n++, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_fchown(loop, reqs + n++, 0, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_fdatasync(loop, reqs + n++, 0, fs_cb));
  ASSERT(0 == uv_fs_fstat(loop, reqs + n++, 0, fs_cb));
  ASSERT(0 == uv_fs_fsync(loop, reqs + n++, 0, fs_cb));
  ASSERT(0 == uv_fs_ftruncate(loop, reqs + n++, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_futime(loop, reqs + n++, 0, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_link(loop, reqs + n++, "/", "/", fs_cb));
  ASSERT(0 == uv_fs_lstat(loop, reqs + n++, "/", fs_cb));
  ASSERT(0 == uv_fs_mkdir(loop, reqs + n++, "/", 0, fs_cb));
  ASSERT(0 == uv_fs_open(loop, reqs + n++, "/", 0, 0, fs_cb));
  ASSERT(0 == uv_fs_read(loop, reqs + n++, 0, NULL, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_scandir(loop, reqs + n++, "/", 0, fs_cb));
  ASSERT(0 == uv_fs_readlink(loop, reqs + n++, "/", fs_cb));
  ASSERT(0 == uv_fs_rename(loop, reqs + n++, "/", "/", fs_cb));
  ASSERT(0 == uv_fs_mkdir(loop, reqs + n++, "/", 0, fs_cb));
  ASSERT(0 == uv_fs_sendfile(loop, reqs + n++, 0, 0, 0, 0, fs_cb));
  ASSERT(0 == uv_fs_stat(loop, reqs + n++, "/", fs_cb));
  ASSERT(0 == uv_fs_symlink(loop, reqs + n++, "/", "/", 0, fs_cb));
  ASSERT(0 == uv_fs_unlink(loop, reqs + n++, "/", fs_cb));
  ASSERT(0 == uv_fs_utime(loop, reqs + n++, "/", 0, 0, fs_cb));
  ASSERT(0 == uv_fs_write(loop, reqs + n++, 0, NULL, 0, 0, fs_cb));
  ASSERT(n == ARRAY_SIZE(reqs));

  ASSERT(0 == uv_timer_init(loop, &ci.timer_handle));
  ASSERT(0 == uv_timer_start(&ci.timer_handle, timer_cb, 10, 0));
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  ASSERT(n == fs_cb_called);
  ASSERT(1 == timer_cb_called);

  cleanup_threadpool();

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(threadpool_cancel_single) {
  uv_loop_t* loop;
  uv_work_t req;
  int cancelled;
  int i;

  /* Work requests are executed synchronously when there is no threadpool. */
  if (no_threadpool())
    RETURN_SKIP("Cancellation doesn't work in synchronous work mode.");

  loop = uv_default_loop();
  for (i = 0; i < 5000; i++) {
    req.data = NULL;
    ASSERT(0 == uv_queue_work(loop, &req, nop_work_cb, nop_done_cb));

    cancelled = uv_cancel((uv_req_t*) &req);
    if (cancelled == 0)
      break;

    ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  }

  if (cancelled != 0) {
    fputs("Failed to cancel a work req in 5,000 iterations, giving up.\n",
          stderr);
    return 1;
  }

  ASSERT(req.data == NULL);
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));
  ASSERT(req.data != NULL);  /* Should have been updated by nop_done_cb(). */

  MAKE_VALGRIND_HAPPY();
  return 0;
}
