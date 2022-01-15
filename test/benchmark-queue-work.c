#include "task.h"
#include "uv.h"

static int done = 0;
static unsigned events = 0;

static unsigned fastrand(void) {
  static unsigned g = 0;
  g = g * 214013 + 2531011;
  return g;
}

static void work_cb(uv_work_t *req) { 
  fastrand();
}

static void after_work_cb(uv_work_t *req, int status) {
  static uv_work_t handle;

  events++;
  if (!done)
    ASSERT(0 == uv_queue_work(req->loop, &handle, work_cb, after_work_cb));
}

static void timer_cb(uv_timer_t *handle) { done = 1; }

BENCHMARK_IMPL(queue_work) {
  uv_timer_t timer_handle;
  uv_work_t handle;
  uv_loop_t *loop;
  int timeout;

  loop = uv_default_loop();
  timeout = 5000;

  ASSERT(0 == uv_timer_init(loop, &timer_handle));
  ASSERT(0 == uv_timer_start(&timer_handle, timer_cb, timeout, 0));

  ASSERT(0 == uv_queue_work(loop, &handle, work_cb, after_work_cb));
  ASSERT(0 == uv_run(loop, UV_RUN_DEFAULT));

  printf("%s async jobs in %.1f seconds (%s/s)\n", fmt(events), timeout / 1000.,
         fmt(events / (timeout / 1000.)));

  MAKE_VALGRIND_HAPPY();
  return 0;
}
