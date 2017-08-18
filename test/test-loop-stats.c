#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <limits.h>

static uv_timer_t timer_handle;

static void timer_cb(uv_timer_t* handle) {}

TEST_IMPL(loop_stats_sync) {
  uv_stats_info_t info;

  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 0);

  uv_loop_stats(uv_default_loop(), &info);
  ASSERT(info.last_stats_cb == 0);
  ASSERT(info.loop_enter == 0);
  ASSERT(info.loop_exit == 0);
  ASSERT(info.tick_start == 0);
  ASSERT(info.tick_end == 0);
  ASSERT(info.idle_start == 0);
  ASSERT(info.idle_end == 0);
  ASSERT(info.prepare_start == 0);
  ASSERT(info.prepare_end == 0);
  ASSERT(info.poll_start == 0);
  ASSERT(info.poll_end == 0);
  ASSERT(info.check_start == 0);
  ASSERT(info.check_end == 0);
  ASSERT(info.tick_count == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  uv_loop_stats(uv_default_loop(), &info);
  ASSERT(info.last_stats_cb == 0);
  ASSERT(info.loop_enter > 0);
  ASSERT(info.loop_exit > 0);
  ASSERT(info.tick_start > 0);
  ASSERT(info.tick_end > 0);
  ASSERT(info.idle_start > 0);
  ASSERT(info.idle_end > 0);
  ASSERT(info.prepare_start > 0);
  ASSERT(info.prepare_end > 0);
  ASSERT(info.poll_start > 0);
  ASSERT(info.poll_end > 0);
  ASSERT(info.check_start > 0);
  ASSERT(info.check_end > 0);
  ASSERT(info.tick_count == 2);

  return 0;
}

static uint64_t on_stats_cb_tick_called = 0;
void on_stats_cb_tick(uv_stats_info_t* info) {
  ASSERT(info->last_stats_cb > 0);
  ASSERT(info->loop_enter > 0);
  ASSERT(info->loop_exit == 0);  /* loop will not have exited yet */
  ASSERT(info->tick_start > 0);
  ASSERT(info->tick_end > 0);
  ASSERT(info->idle_start > 0);
  ASSERT(info->idle_end > 0);
  ASSERT(info->prepare_start > 0);
  ASSERT(info->prepare_end > 0);
  ASSERT(info->poll_start > 0);
  ASSERT(info->poll_end > 0);
  ASSERT(info->check_start > 0);
  ASSERT(info->check_end > 0);
  on_stats_cb_tick_called = info->tick_count;
}

TEST_IMPL(loop_stats_cb_tick) {
  uv_stats_config_t config = { UV_LOOP_STATS_TICK, 0, on_stats_cb_tick };
  uv_loop_configure(uv_default_loop(), UV_LOOP_STATS, &config);

  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(on_stats_cb_tick_called == 2);

  return 0;
}

static unsigned int on_stats_cb_count_called = 0;
void on_stats_cb_count(uv_stats_info_t* info) {
  ASSERT(on_stats_cb_count_called == 0);
  ASSERT(info->last_stats_cb > 0);
  ASSERT(info->loop_enter > 0);
  ASSERT(info->loop_exit == 0);  /* loop will not have exited yet */
  ASSERT(info->tick_start > 0);
  ASSERT(info->tick_end > 0);
  ASSERT(info->idle_start > 0);
  ASSERT(info->idle_end > 0);
  ASSERT(info->prepare_start > 0);
  ASSERT(info->prepare_end > 0);
  ASSERT(info->poll_start > 0);
  ASSERT(info->poll_end > 0);
  ASSERT(info->check_start > 0);
  ASSERT(info->check_end > 0);
  ASSERT(info->tick_count == 2);
  on_stats_cb_count_called++;
}

TEST_IMPL(loop_stats_cb_count) {
  uv_stats_config_t config = { UV_LOOP_STATS_COUNT, 2, on_stats_cb_count };
  uv_loop_configure(uv_default_loop(), UV_LOOP_STATS, &config);

  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(on_stats_cb_count_called == 1);

  return 0;
}

static unsigned int on_stats_cb_time_called = 0;
void on_stats_cb_time(uv_stats_info_t* info) {
  ASSERT(info->last_stats_cb > 0);
  ASSERT(info->loop_enter > 0);
  ASSERT(info->loop_exit == 0);  /* loop will not have exited yet */
  ASSERT(info->tick_start > 0);
  ASSERT(info->tick_end > 0);
  ASSERT(info->idle_start > 0);
  ASSERT(info->idle_end > 0);
  ASSERT(info->prepare_start > 0);
  ASSERT(info->prepare_end > 0);
  ASSERT(info->poll_start > 0);
  ASSERT(info->poll_end > 0);
  ASSERT(info->check_start > 0);
  ASSERT(info->check_end > 0);
  on_stats_cb_time_called++;
}

TEST_IMPL(loop_stats_cb_time) {
  uv_stats_config_t config = { UV_LOOP_STATS_TIME, 1 * 1e6, on_stats_cb_time };
  uv_loop_configure(uv_default_loop(), UV_LOOP_STATS, &config);

  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(on_stats_cb_time_called == 1);

  return 0;
}
