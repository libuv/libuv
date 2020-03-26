#include "uv.h"
#include "task.h"
#include <string.h> /* memset */

#define NS_MS 1000000


static void timer_spin_cb(uv_timer_t* handle) {
  uint64_t t;

  (*(int*) handle->data)++;
  t = uv_hrtime();
  /* Spin for 500 ms to spin loop time out of the delta check. */
  while (uv_hrtime() - t < 600 * NS_MS) { }
}


TEST_IMPL(metrics_idle_time) {
  const uint64_t timeout = 1000;
  uv_timer_t timer;
  uint64_t idle_time;
  int cntr;

  cntr = 0;
  timer.data = &cntr;

  ASSERT_EQ(0, uv_loop_configure(uv_default_loop(), UV_METRICS_IDLE_TIME));
  ASSERT_EQ(0, uv_timer_init(uv_default_loop(), &timer));
  ASSERT_EQ(0, uv_timer_start(&timer, timer_spin_cb, timeout, 0));

  ASSERT_EQ(0, uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_GT(cntr, 0);

  idle_time = uv_metrics_idle_time(uv_default_loop());

  /* Permissive check that the idle time matches within the timeout ±500 ms. */
  ASSERT((idle_time <= (timeout + 500) * NS_MS) &&
         (idle_time >= (timeout - 500) * NS_MS));

  MAKE_VALGRIND_HAPPY();
  return 0;
}


static void metrics_routine_cb(void* arg) {
  const uint64_t timeout = 1000;
  uv_loop_t loop;
  uv_timer_t timer;
  uint64_t idle_time;
  int cntr;

  cntr = 0;
  timer.data = &cntr;

  ASSERT_EQ(0, uv_loop_init(&loop));
  ASSERT_EQ(0, uv_loop_configure(&loop, UV_METRICS_IDLE_TIME));
  ASSERT_EQ(0, uv_timer_init(&loop, &timer));
  ASSERT_EQ(0, uv_timer_start(&timer, timer_spin_cb, timeout, 0));

  ASSERT_EQ(0, uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_GT(cntr, 0);

  idle_time = uv_metrics_idle_time(&loop);

  /* Only checking that idle time is greater than the lower bound since there
   * may have been thread contention, causing the event loop to be delayed in
   * the idle phase longer than expected.
   */
  ASSERT_GE(idle_time, (timeout - 500) * NS_MS);

  close_loop(&loop);
  ASSERT_EQ(0, uv_loop_close(&loop));
}


TEST_IMPL(metrics_idle_time_thread) {
  uv_thread_t threads[5];
  int i;

  for (i = 0; i < 5; i++) {
    ASSERT_EQ(0, uv_thread_create(&threads[i], metrics_routine_cb, NULL));
  }

  for (i = 0; i < 5; i++) {
    uv_thread_join(&threads[i]);
  }

  return 0;
}


static void timer_noop_cb(uv_timer_t* handle) {
  (*(int*) handle->data)++;
}


TEST_IMPL(metrics_idle_time_zero) {
  uv_timer_t timer;
  int cntr;

  cntr = 0;
  timer.data = &cntr;
  ASSERT_EQ(0, uv_loop_configure(uv_default_loop(), UV_METRICS_IDLE_TIME));
  ASSERT_EQ(0, uv_timer_init(uv_default_loop(), &timer));
  ASSERT_EQ(0, uv_timer_start(&timer, timer_noop_cb, 0, 0));

  ASSERT_EQ(0, uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_GT(cntr, 0);
  ASSERT_EQ(0, uv_metrics_idle_time(uv_default_loop()));

  MAKE_VALGRIND_HAPPY();
  return 0;
}
