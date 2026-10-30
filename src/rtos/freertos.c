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
#include "uv-common.h"
#include "heap-inl.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdarg.h>  /* va_list */
#include <string.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif  /* __GNUC__ */

#include "atomic.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  /* __GNUC__ */

#ifdef __GNUC__
#define UNREACHABLE() __builtin_trap()
#else
#define UNREACHABLE() for (;;);
#endif

#if INCLUDE_uxTaskPriorityGet == 0
#error "libuv requires INCLUDE_uxTaskPriorityGet"
#endif

#if INCLUDE_vTaskDelay == 0
#error "libuv requires INCLUDE_vTaskDelay"
#endif

#if INCLUDE_vTaskDelete == 0
#error "libuv requires INCLUDE_vTaskDelete"
#endif

/* We don't use vTaskSuspend ourselves but without INCLUDE_vTaskSuspend,
 * portMAX_DELAY is a finite value, and we want it to mean "indefinitely."
 */
#if INCLUDE_vTaskSuspend == 0
#error "libuv requires INCLUDE_vTaskSuspend"
#endif

#if INCLUDE_xTaskGetSchedulerState == 0
#error "libuv requires INCLUDE_xTaskGetSchedulerState"
#endif

#if configUSE_COUNTING_SEMAPHORES == 0
#error "libuv requires configUSE_COUNTING_SEMAPHORES"
#endif

#if configUSE_MUTEXES == 0
#error "libuv requires configUSE_MUTEXES"
#endif

#if configUSE_TASK_NOTIFICATIONS == 0
#error "libuv requires configUSE_TASK_NOTIFICATIONS"
#endif

#ifndef UV_FREERTOS_TLS_BASE
#define UV_FREERTOS_TLS_BASE 0
#endif

#ifndef UV_FREERTOS_TLS_SIZE
#define UV_FREERTOS_TLS_SIZE                                                  \
    (configNUM_THREAD_LOCAL_STORAGE_POINTERS - UV_FREERTOS_TLS_BASE)
#endif

#if UV_FREERTOS_TLS_BASE < 0 ||                                               \
    UV_FREERTOS_TLS_SIZE < 0 ||                                               \
    UV_FREERTOS_TLS_BASE + UV_FREERTOS_TLS_SIZE > configNUM_THREAD_LOCAL_STORAGE_POINTERS
#error "bad UV_FREERTOS_TLS_BASE or UV_FREERTOS_TLS_SIZE"
#endif

#ifndef UV_FREERTOS_NOTIFY_INDEX
#define UV_FREERTOS_NOTIFY_INDEX (configTASK_NOTIFICATION_ARRAY_ENTRIES - 1)
#endif

#if UV_FREERTOS_NOTIFY_INDEX < 0 ||                                           \
    UV_FREERTOS_NOTIFY_INDEX >= configTASK_NOTIFICATION_ARRAY_ENTRIES
#error "UV_FREERTOS_NOTIFY_INDEX out of range"
#endif

#undef uv__metrics_inc_loop_count
#undef uv__metrics_update_idle_time

#define uv__metrics_inc_loop_count(loop)
#define uv__metrics_update_idle_time(loop)

#if configUSE_RECURSIVE_MUTEXES == 0
#define xSemaphoreTakeRecursive(m, t)               (pdFALSE)
#define xSemaphoreGiveRecursive(m)
#endif

static TickType_t uv__ticks(uint64_t timeout, uint64_t precision) {
  uint64_t ticks;
  uint64_t secs;
  uint64_t rem;
  uint64_t hz;

  if (timeout == 0)
    return 0;
  hz = configTICK_RATE_HZ;
  secs = timeout / precision;
  rem = timeout % precision;
  ticks = rem * hz;
  ticks += precision - 1;
  ticks /= precision;
  ticks += secs * hz;
  ticks += 1;
  if (ticks < portMAX_DELAY)
    return ticks;
  return portMAX_DELAY - 1;  /* don't block forever */
}

static uint64_t uv__time(uint64_t precision) {
  TimeOut_t t;
  uint64_t hz;
  uint64_t rem;
  uint64_t secs;
  uint64_t ticks;

  /* TickType_t can be 32 bits and overflow after ~49 days (at 1 kHz)
   * so we can't use xTaskGetTickCount().
   */
  vTaskSetTimeOutState(&t);
  /* portMAX_DELAY + 1 wraps around to 0 when TickType_t is 64 bits but that's
   * okay because we don't expect timer overflows on such systems before the
   * heat death of the universe anyway.
   * xOverflowCount can be as small as a signed 8 bits value, so we have to be
   * careful with integer promotion; not that libuv is expected to run well
   * on such systems.
   */
  ticks = (uint64_t) portMAX_DELAY + 1;
  ticks *= (UBaseType_t) t.xOverflowCount;  /* signed -> unsigned */
  ticks += t.xTimeOnEntering;
  /* We don't use something like pdTICKS_TO_MS because we don't want to lose
   * precision on systems with high-precision clocks, or systems running at
   * odd tick rates like 333 Hz.
   */
  hz = configTICK_RATE_HZ;
  secs = ticks / hz;
  rem = ticks % hz;
  rem *= precision;
  rem /= hz;
  return precision * secs + rem;
}

#define UV_LOOP_WATCHER_DEFINE(name, type)                                    \
  int uv_##name##_init(uv_loop_t* loop, uv_##name##_t* handle) {              \
    uv__handle_init(loop, (uv_handle_t*)handle, UV_##type);                   \
    handle->name##_cb = NULL;                                                 \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##name##_start(uv_##name##_t* handle, uv_##name##_cb cb) {           \
    if (uv__is_active(handle)) return 0;                                      \
    if (cb == NULL) return UV_EINVAL;                                         \
    uv__queue_insert_head(&handle->loop->name##_handles, &handle->queue);     \
    handle->name##_cb = cb;                                                   \
    uv__handle_start(handle);                                                 \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##name##_stop(uv_##name##_t* handle) {                               \
    if (!uv__is_active(handle)) return 0;                                     \
    uv__queue_remove(&handle->queue);                                         \
    uv__handle_stop(handle);                                                  \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  void uv__run_##name(uv_loop_t* loop) {                                      \
    uv_##name##_t* h;                                                         \
    struct uv__queue queue;                                                   \
    struct uv__queue* q;                                                      \
    uv__queue_move(&loop->name##_handles, &queue);                            \
    while (!uv__queue_empty(&queue)) {                                        \
      q = uv__queue_head(&queue);                                             \
      h = uv__queue_data(q, uv_##name##_t, queue);                            \
      uv__queue_remove(q);                                                    \
      uv__queue_insert_tail(&loop->name##_handles, q);                        \
      h->name##_cb(h);                                                        \
    }                                                                         \
  }                                                                           \
                                                                              \
  void uv__##name##_close(uv_##name##_t* handle) {                            \
    uv_##name##_stop(handle);                                                 \
  }

UV_LOOP_WATCHER_DEFINE(prepare, PREPARE)
UV_LOOP_WATCHER_DEFINE(check, CHECK)
UV_LOOP_WATCHER_DEFINE(idle, IDLE)

void uv__make_close_pending(uv_handle_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  handle->next_closing = handle->loop->closing_handles;
  handle->loop->closing_handles = handle;
}

static void uv__finish_close(uv_handle_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  handle->flags |= UV_HANDLE_CLOSED;
  uv__handle_unref(handle);
  uv__queue_remove(&handle->handle_queue);
  if (handle->close_cb)
    handle->close_cb(handle);
}

static void uv__run_pending(uv_loop_t* loop) {
  /* Nothing to do for now. */
}

static void uv__run_closing_handles(uv_loop_t* loop) {
  uv_handle_t* p;
  uv_handle_t* q;

  p = loop->closing_handles;
  loop->closing_handles = NULL;

  while (p) {
    q = p->next_closing;
    uv__finish_close(p);
    p = q;
  }
}

static void uv__io_poll(uv_loop_t* loop, int timeout) {
  struct uv__queue queue;
  struct uv__queue* q;
  TickType_t ticks;
  uint32_t count;
  uv_async_t* h;

  ticks = portMAX_DELAY;
  if (timeout >= 0)
    ticks = uv__ticks(timeout, 1000);  /* ms -> ticks */
  count = ulTaskNotifyTakeIndexed(UV_FREERTOS_NOTIFY_INDEX,
                                  pdTRUE,  /* clear count on exit */
                                  ticks);
  if (count > 0) {
    uv__queue_move(&loop->async_handles, &queue);
    while (!uv__queue_empty(&queue)) {
      q = uv__queue_head(&queue);
      h = uv__queue_data(q, uv_async_t, queue);
      uv__queue_remove(q);
      uv__queue_insert_tail(&loop->async_handles, q);
      if (h->async_cb != NULL)
        if (1 & atomic_fetch_and((_Atomic int*) &h->pending, ~1))
          h->async_cb(h);
    }
  }
}

static void uv__update_time(uv_loop_t* loop) {
  loop->time = uv__time(1000);  /* millis */
}

static int uv__loop_alive(const uv_loop_t* loop) {
  return uv__has_active_handles(loop) ||
         uv__has_active_reqs(loop) ||
         !uv__queue_empty(&loop->pending_queue) ||
         loop->closing_handles != NULL;
}

static int uv__backend_timeout(const uv_loop_t* loop) {
  if (loop->stop_flag == 0 &&
      (uv__has_active_handles(loop) || uv__has_active_reqs(loop)) &&
      uv__queue_empty(&loop->pending_queue) &&
      uv__queue_empty(&loop->idle_handles) &&
      loop->closing_handles == NULL)
    return uv__next_timeout(loop);
  return 0;
}

void uv__loop_close(uv_loop_t* loop) {
}

int uv__loop_configure(uv_loop_t* loop, uv_loop_option option, va_list ap) {
  return UV_ENOSYS;  /* No supported options. */
}

int uv__read_start(uv_stream_t* stream,
                   uv_alloc_cb alloc_cb,
                   uv_read_cb read_cb) {
  return UV_ENOTSUP;
}

void uv__process_title_cleanup(void) {
  /* No process title, nothing to clean up. */
}

void uv__signal_cleanup(void) {
  /* No signals, nothing to clean up. */
}

void uv__threadpool_cleanup(void) {
  /* No threadpool, nothing to clean up. */
}

int uv_loop_init(uv_loop_t* loop) {
  void* saved_data;

  saved_data = loop->data;
  memset(loop, 0, sizeof(*loop));
  loop->data = saved_data;
  loop->current_task = xTaskGetCurrentTaskHandle();

  heap_init((struct heap*) &loop->timer_heap);
  uv__queue_init(&loop->async_handles);
  uv__queue_init(&loop->check_handles);
  uv__queue_init(&loop->handle_queue);
  uv__queue_init(&loop->idle_handles);
  uv__queue_init(&loop->pending_queue);
  uv__queue_init(&loop->prepare_handles);
  uv__update_time(loop);

  return 0;
}

int uv_loop_alive(const uv_loop_t* loop) {
  return uv__loop_alive(loop);
}

int uv_loop_fork(uv_loop_t* loop) {
  return UV_ENOTSUP;
}

int uv_run(uv_loop_t* loop, uv_run_mode mode) {
  int can_sleep;
  int timeout;
  int r;

  r = uv__loop_alive(loop);
  if (!r)
    uv__update_time(loop);

  /* Maintain backwards compatibility by processing timers before entering the
   * while loop for UV_RUN_DEFAULT. Otherwise timers only need to be executed
   * once, which should be done after polling in order to maintain proper
   * execution order of the conceptual event loop. */
  if (mode == UV_RUN_DEFAULT && r != 0 && loop->stop_flag == 0) {
    uv__update_time(loop);
    uv__run_timers(loop);
  }

  while (r != 0 && loop->stop_flag == 0) {
    can_sleep =
        uv__queue_empty(&loop->pending_queue) &&
        uv__queue_empty(&loop->idle_handles);

    uv__run_pending(loop);
    uv__run_idle(loop);
    uv__run_prepare(loop);

    timeout = 0;
    if ((mode == UV_RUN_ONCE && can_sleep) || mode == UV_RUN_DEFAULT)
      timeout = uv__backend_timeout(loop);

    uv__metrics_inc_loop_count(loop);

    uv__io_poll(loop, timeout);

    /* Process immediate callbacks (e.g. write_cb) a small fixed number of
     * times to avoid loop starvation.*/
    for (r = 0; r < 8 && !uv__queue_empty(&loop->pending_queue); r++)
      uv__run_pending(loop);

    /* Run one final update on the provider_idle_time in case uv__io_poll
     * returned because the timeout expired, but no events were received. This
     * call will be ignored if the provider_entry_time was either never set (if
     * the timeout == 0) or was already updated b/c an event was received.
     */
    uv__metrics_update_idle_time(loop);

    uv__run_check(loop);
    uv__run_closing_handles(loop);

    uv__update_time(loop);
    uv__run_timers(loop);

    r = uv__loop_alive(loop);
    if (mode == UV_RUN_ONCE || mode == UV_RUN_NOWAIT)
      break;
  }

  /* The if statement lets gcc compile it to a conditional store. Avoids
   * dirtying a cache line.
   */
  if (loop->stop_flag != 0)
    loop->stop_flag = 0;

  return r;
}

int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;
  uv__queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);
  return 0;
}

void uv__async_notify(uv_async_t* handle) {
  TaskHandle_t current_task;

  current_task = handle->loop->current_task;
  if (current_task != NULL)
    xTaskNotifyGiveIndexed(current_task, UV_FREERTOS_NOTIFY_INDEX);
}

static void uv__async_close(uv_async_t* handle) {
  uv__async_spin(handle);
  uv__queue_remove(&handle->queue);
  uv__handle_stop(handle);
}

void uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
  assert(!uv__is_closing(handle));
  handle->flags |= UV_HANDLE_CLOSING;
  handle->close_cb = close_cb;
  switch (handle->type) {
  case UV_ASYNC:
    uv__async_close((uv_async_t*) handle);
    break;
  case UV_CHECK:
    uv__check_close((uv_check_t*) handle);
    break;
  case UV_IDLE:
    uv__idle_close((uv_idle_t*) handle);
    break;
  case UV_PREPARE:
    uv__prepare_close((uv_prepare_t*) handle);
    break;
  case UV_TIMER:
    uv__timer_close((uv_timer_t*) handle);
    break;
  default:
    /* Do nothing. */
  }
  uv__make_close_pending(handle);
}

void uv_update_time(uv_loop_t* loop) {
  uv__update_time(loop);
}

int uv_is_closing(const uv_handle_t* handle) {
  return uv__is_closing(handle);
}

int uv_backend_fd(const uv_loop_t* loop) {
  return UV_ENOTSUP;
}

int uv_backend_timeout(const uv_loop_t* loop) {
  return UV_ENOTSUP;
}

int uv_shutdown(uv_shutdown_t* req, uv_stream_t* handle, uv_shutdown_cb cb) {
  return UV_ENOTSUP;
}

int uv_is_active(const uv_handle_t* handle) {
  return uv__is_active(handle);
}

uint64_t uv_hrtime(void) {
  return uv__time(1000000000ull);  /* nanos */
}

void uv_once(uv_once_t* once, void (*callback)(void)) {
  if (Atomic_CompareAndSwap_u32(&once->flag, 1, 0)) {
    callback();
    Atomic_Increment_u32(&once->flag);
  } else while (2 != Atomic_Add_u32(&once->flag, 0)) {
    vTaskDelay(1);
  }
}

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  UBaseType_t max;

  max = (UBaseType_t) -1;
#if configSUPPORT_STATIC_ALLOCATION == 1
  sem->sema = xSemaphoreCreateCountingStatic(max, value, &sem->sema_buf);
#else
  sem->sema = xSemaphoreCreateCounting(max, value);
#endif
  if (sem->sema == NULL)
    return UV_ENOMEM;
  return 0;
}

void uv_sem_destroy(uv_sem_t* sem) {
  vSemaphoreDelete(sem->sema);
}

void uv_sem_post(uv_sem_t* sem) {
  xSemaphoreGive(sem->sema);
}

void uv_sem_wait(uv_sem_t* sem) {
  xSemaphoreTake(sem->sema, portMAX_DELAY);
}

int uv_sem_trywait(uv_sem_t* sem) {
  return xSemaphoreTake(sem->sema, 0) ? 0 : UV_EAGAIN;
}

int uv_sem_timedwait(uv_sem_t* sem, uint64_t timeout) {
  TickType_t ticks;

  ticks = portMAX_DELAY;
  if (timeout != (uint64_t) -1)
    ticks = uv__ticks(timeout, 1000000000ull);  /* ns -> ticks */
  if (xSemaphoreTake(sem->sema, ticks))
    return 0;
  return UV_ETIMEDOUT;
}

int uv_mutex_init(uv_mutex_t* mutex) {
#if configSUPPORT_STATIC_ALLOCATION == 1
  mutex->sema = xSemaphoreCreateMutexStatic(&mutex->sema_buf);
#else
  mutex->sema = xSemaphoreCreateMutex();
#endif
  mutex->recursive = 0;
  if (mutex->sema == NULL)
    return UV_ENOMEM;
  return 0;
}

int uv_mutex_init_recursive(uv_mutex_t* mutex) {
#if configUSE_RECURSIVE_MUTEXES == 1
#if configSUPPORT_STATIC_ALLOCATION == 1
  mutex->sema = xSemaphoreCreateRecursiveMutexStatic(&mutex->sema_buf);
#else
  mutex->sema = xSemaphoreCreateRecursiveMutex();
#endif
  mutex->recursive = 1;
  if (mutex->sema == NULL)
    return UV_ENOMEM;
  return 0;
#else
  return UV_ENOTSUP;
#endif
}

void uv_mutex_destroy(uv_mutex_t* mutex) {
  vSemaphoreDelete(mutex->sema);
}

void uv_mutex_lock(uv_mutex_t* mutex) {
  if (mutex->recursive) {
    if (pdTRUE == xSemaphoreTakeRecursive(mutex->sema, portMAX_DELAY))
      return;
    UNREACHABLE();
  } else {
    if (pdTRUE == xSemaphoreTake(mutex->sema, portMAX_DELAY))
      return;
    UNREACHABLE();
  }
}

int uv_mutex_trylock(uv_mutex_t* mutex) {
  if (mutex->recursive) {
    if (pdTRUE == xSemaphoreTakeRecursive(mutex->sema, 0))
      return 0;
  } else {
    if (pdTRUE == xSemaphoreTake(mutex->sema, 0))
      return 0;
  }
  return UV_EBUSY;
}

void uv_mutex_unlock(uv_mutex_t* mutex) {
  if (mutex->recursive) {
    xSemaphoreGiveRecursive(mutex->sema);
  } else {
    xSemaphoreGive(mutex->sema);
  }
}

int uv_rwlock_init(uv_rwlock_t* rwlock) {
  int rc;

  rc = uv_mutex_init(&rwlock->mutex);
  if (rc)
    return rc;
  rc = uv_sem_init(&rwlock->sem, 1);
  if (rc) {
    uv_mutex_destroy(&rwlock->mutex);
    return rc;
  }
  rwlock->count = 0;
  return 0;
}

void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  uv_sem_destroy(&rwlock->sem);
  uv_mutex_destroy(&rwlock->mutex);
}

void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->mutex);
  if (++rwlock->count == 1)
    uv_sem_wait(&rwlock->sem);
  uv_mutex_unlock(&rwlock->mutex);
}

int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  int rc;

  rc = uv_mutex_trylock(&rwlock->mutex);
  if (rc == 0) {
    if (++rwlock->count == 1 && uv_sem_trywait(&rwlock->sem)) {
      rwlock->count--;
      rc = UV_EBUSY;
    }
    uv_mutex_unlock(&rwlock->mutex);
  }
  return rc;
}

void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->mutex);
  if (--rwlock->count == 0)
    uv_sem_post(&rwlock->sem);
  uv_mutex_unlock(&rwlock->mutex);
}

void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  uv_sem_wait(&rwlock->sem);
}

int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  return uv_sem_trywait(&rwlock->sem) ? UV_EBUSY : 0;
}

void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  uv_sem_post(&rwlock->sem);
}

/* TODO(bnoordhuis) not super efficient but correct and simple */
struct uv__cond_ctx {
  struct uv__queue queue;
  uv_sem_t sem;
};

int uv_cond_init(uv_cond_t* cond) {
  int rc;

  rc = uv_mutex_init(&cond->mutex);
  uv__queue_init(&cond->queue);
  return rc;
}

void uv_cond_destroy(uv_cond_t* cond) {
  uv_mutex_destroy(&cond->mutex);
}

void uv_cond_signal(uv_cond_t* cond) {
  struct uv__cond_ctx* ctx;
  struct uv__queue* q;

  uv_mutex_lock(&cond->mutex);
  if (!uv__queue_empty(&cond->queue)) {
    q = uv__queue_head(&cond->queue);
    ctx = uv__queue_data(q, struct uv__cond_ctx, queue);
    uv__queue_remove(&ctx->queue);
    uv_sem_post(&ctx->sem);
  }
  uv_mutex_unlock(&cond->mutex);
}

void uv_cond_broadcast(uv_cond_t* cond) {
  struct uv__cond_ctx* ctx;
  struct uv__queue* q;

  uv_mutex_lock(&cond->mutex);
  while (!uv__queue_empty(&cond->queue)) {
    q = uv__queue_head(&cond->queue);
    ctx = uv__queue_data(q, struct uv__cond_ctx, queue);
    uv__queue_remove(&ctx->queue);
    uv_sem_post(&ctx->sem);
  }
  uv_mutex_unlock(&cond->mutex);
}

void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  /* Can fail with UV_ENOMEM but we can't return that error to the caller.
   * Theoretically harmless because the caller checks the predicate and loops,
   * but if that turns into a busy loop, then of course it's not so harmless.
   */
  uv_cond_timedwait(cond, mutex, -1);
}

int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  struct uv__cond_ctx ctx;
  int rc;

  rc = uv_sem_init(&ctx.sem, 0);
  if (rc)
    return rc;
  uv_mutex_lock(&cond->mutex);
  uv__queue_insert_tail(&cond->queue, &ctx.queue);
  uv_mutex_unlock(&cond->mutex);
  uv_mutex_unlock(mutex);
  rc = uv_sem_timedwait(&ctx.sem, timeout);
  uv_mutex_lock(&cond->mutex);
  if (uv__queue_empty(&ctx.queue)) {
    rc = 0;  /* Signaled, possibly after timeout expired. */
  } else {
    uv__queue_remove(&ctx.queue);
  }
  uv_mutex_unlock(&cond->mutex);
  uv_sem_destroy(&ctx.sem);
  uv_mutex_lock(mutex);
  return rc;
}

int uv_barrier_init(uv_barrier_t* barrier, unsigned int count) {
  int rc;

  rc = uv_mutex_init(&barrier->mutex);
  if (rc)
    return rc;
  rc = uv_cond_init(&barrier->cond);
  if (rc) {
    uv_mutex_destroy(&barrier->mutex);
    return rc;
  }
  barrier->threshold = count;
  barrier->out = 0;
  barrier->in = 0;
  return 0;
}

void uv_barrier_destroy(uv_barrier_t* barrier) {
  uv_mutex_destroy(&barrier->mutex);
  uv_cond_destroy(&barrier->cond);
}

int uv_barrier_wait(uv_barrier_t* barrier) {
  int last;

  uv_mutex_lock(&barrier->mutex);
  while (barrier->out != 0)
    uv_cond_wait(&barrier->cond, &barrier->mutex);
  if (++barrier->in == barrier->threshold) {
    barrier->in = 0;
    barrier->out = barrier->threshold;
    uv_cond_broadcast(&barrier->cond);
  } else {
    do
      uv_cond_wait(&barrier->cond, &barrier->mutex);
    while (barrier->in != 0);
  }
  last = (--barrier->out == 0);
  if (last)
    uv_cond_broadcast(&barrier->cond);
  uv_mutex_unlock(&barrier->mutex);
  return last;
}

enum { UV__THREAD_JOINABLE, UV__THREAD_DETACHED, UV__THREAD_DEAD };

struct uv__thread_task_ctx {
  uv_mutex_t mutex;
  uv_cond_t cond;
  int state;
  void (*entry)(void* arg);
  void* arg;
};

static void uv__thread_task(void* arg) {
  struct uv__thread_task_ctx* ctx = arg;

  ctx->entry(ctx->arg);
  uv_mutex_lock(&ctx->mutex);
  switch (ctx->state) {
  case UV__THREAD_JOINABLE:
    ctx->state = UV__THREAD_DEAD;
    uv_cond_signal(&ctx->cond);
    uv_mutex_unlock(&ctx->mutex);
    break;
  case UV__THREAD_DETACHED:
    uv_mutex_unlock(&ctx->mutex);
    uv_mutex_destroy(&ctx->mutex);
    uv_cond_destroy(&ctx->cond);
    uv__free(ctx);
    break;
  }
  vTaskDelete(NULL);  /* Delete ourselves. Does not return. */
  UNREACHABLE();
}

int uv_thread_create(uv_thread_t* tid, void (*entry)(void* arg), void* arg) {
  uv_thread_options_t params = {UV_THREAD_NO_FLAGS};
  return uv_thread_create_ex(tid, &params, entry, arg);
}

int uv_thread_create_ex(uv_thread_t* tid, const uv_thread_options_t* params,
                        void (*entry)(void* arg), void* arg) {
  struct uv__thread_task_ctx* ctx;
  TaskHandle_t task;
  size_t stack_size;
  size_t stack_words;
  UBaseType_t prio;

  stack_size = 0;
  if (params != NULL) {
    if (params->flags & ~UV_THREAD_HAS_STACK_SIZE)
      return UV_ENOTSUP;
    if (params->flags & UV_THREAD_HAS_STACK_SIZE)
      stack_size = params->stack_size;
  }
  if (stack_size == 0)
    stack_size = 128 * 1024;
  stack_words = stack_size / sizeof(StackType_t);
  if (stack_size % sizeof(StackType_t))
    stack_words++;
  /* Can't go below this. Safe to clamp. */
  if (stack_words < configMINIMAL_STACK_SIZE)
    stack_words = configMINIMAL_STACK_SIZE;
  /* Can't go above that. Unsafe to clamp, return an error. */
  if (stack_words > (configSTACK_DEPTH_TYPE) -1)
    return UV_EINVAL;
  ctx = uv__malloc(sizeof(*ctx));
  if (ctx == NULL)
    goto fail;
  if (uv_mutex_init(&ctx->mutex))
    goto fail;
  if (uv_cond_init(&ctx->cond)) {
    uv_mutex_destroy(&ctx->mutex);
    goto fail;
  }
  ctx->state = UV__THREAD_JOINABLE;
  ctx->entry = entry;
  ctx->arg = arg;
  prio = tskIDLE_PRIORITY + 1;
  if (taskSCHEDULER_NOT_STARTED != xTaskGetSchedulerState())
    prio = uxTaskPriorityGet(NULL);  /* Inherit current task's priority. */
  task = NULL;
  /* Return value check elided, we check |task| afterwards. */
  xTaskCreate(uv__thread_task, "task", stack_words, ctx, prio, &task);
  if (task == NULL) {
    uv_mutex_destroy(&ctx->mutex);
    uv_cond_destroy(&ctx->cond);
fail:
    uv__free(ctx);
    return UV_ENOMEM;
  }
  *tid = (uv_thread_t){task, ctx};
  return 0;
}

int uv_thread_detach(uv_thread_t* tid) {
  struct uv__thread_task_ctx* ctx = tid->ctx;

  if (ctx == NULL)
    return UV_EINVAL;
  uv_mutex_lock(&ctx->mutex);
  if (ctx->state == UV__THREAD_DEAD) {
    tid->ctx = NULL;
    uv_mutex_unlock(&ctx->mutex);
    uv_mutex_destroy(&ctx->mutex);
    uv_cond_destroy(&ctx->cond);
    uv__free(ctx);
  } else {
    ctx->state = UV__THREAD_DETACHED;
    uv_mutex_unlock(&ctx->mutex);
  }
  return 0;
}

int uv_thread_join(uv_thread_t* tid) {
  struct uv__thread_task_ctx* ctx = tid->ctx;

  if (ctx == NULL)
    return UV_EINVAL;
  uv_mutex_lock(&ctx->mutex);
  assert(ctx->state != UV__THREAD_DETACHED);
  while (ctx->state != UV__THREAD_DEAD)
    uv_cond_wait(&ctx->cond, &ctx->mutex);
  tid->ctx = NULL;
  uv_mutex_unlock(&ctx->mutex);
  uv_mutex_destroy(&ctx->mutex);
  uv_cond_destroy(&ctx->cond);
  uv__free(ctx);
  return 0;
}

#if UV_FREERTOS_TLS_SIZE > 0

static volatile uint32_t keys[(UV_FREERTOS_TLS_SIZE + 31) / 32];

int uv_key_create(uv_key_t* key) {
  volatile uint32_t* ptr;
  uint32_t val;
  uint32_t bit;
  uint32_t idx;

  if (taskSCHEDULER_NOT_STARTED == xTaskGetSchedulerState())
    return UV_EINVAL;  /* Scheduler not started yet, TLS unavailable. */
again:
  for (ptr = keys; ptr != ARRAY_END(keys); ptr++) {
    val = Atomic_Add_u32(ptr, 0);
    if (val != (uint32_t) -1)
      goto match;
  }
  return UV_ENOMEM;  /* All slots taken. */
match:
  /* Find the first zero bit, SWAR style. */
  bit = val;
  bit = bit & ~(bit + 1);
  bit = bit - ((bit >> 1) & 0x55555555u);
  bit = (bit & 0x33333333u) + ((bit >> 2) & 0x33333333u);
  bit = (bit + (bit >> 4)) &  0x0F0F0F0Fu;
  bit = (bit * 0x01010101u) >> 24;
  /* Now compute the index. */
  idx = ptr - keys;
  idx = 32*idx + bit;
  /* In case UV_FREERTOS_TLS_SIZE % 32 != 0. */
  if (idx >= UV_FREERTOS_TLS_SIZE)
    return UV_ENOMEM;
  if (!Atomic_CompareAndSwap_u32(ptr, val | 1u<<bit, val))
    goto again;
  key->idx = idx + 1;  /* +1 to catch zero init bugs. */
  uv_key_set(key, NULL);
  return 0;
}

void uv_key_delete(uv_key_t* key) {
  uint32_t idx;

  idx = key->idx;
  if (idx != 0) {
    idx = idx - 1;
    vTaskSetThreadLocalStoragePointer(NULL, UV_FREERTOS_TLS_BASE+idx, NULL);
    Atomic_AND_u32(&keys[idx / 32], ~(1u << (idx & 31)));
    key->idx = 0;
  }
}

void* uv_key_get(uv_key_t* key) {
  uint32_t idx;

  idx = key->idx;
  if (idx != 0) {
    idx = idx - 1;
    return pvTaskGetThreadLocalStoragePointer(NULL, UV_FREERTOS_TLS_BASE+idx);
  }
  return NULL;
}

void uv_key_set(uv_key_t* key, void* value) {
  uint32_t idx;

  idx = key->idx;
  if (idx != 0) {
    idx = idx - 1;
    vTaskSetThreadLocalStoragePointer(NULL, UV_FREERTOS_TLS_BASE+idx, value);
  }
}

#else

int uv_key_create(uv_key_t* key) {
  return UV_ENOTSUP;
}

void uv_key_delete(uv_key_t* key) {
}

void* uv_key_get(uv_key_t* key) {
  return NULL;
}

void uv_key_set(uv_key_t* key, void* value) {
}

#endif  /* UV_FREERTOS_TLS_SIZE */

int uv_thread_getcpu(void) {
  return portGET_CORE_ID();
}

unsigned int uv_available_parallelism(void) {
  return configNUMBER_OF_CORES;
}

uv_thread_t uv_thread_self(void) {
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return (uv_thread_t){task, NULL};
}

int uv_thread_equal(const uv_thread_t* a, const uv_thread_t* b) {
  return a->task == b->task;
}

int uv_thread_getaffinity(uv_thread_t* tid, char* cpumask, size_t mask_size) {
  return UV_ENOTSUP;
}

int uv_thread_setaffinity(uv_thread_t* tid, char* cpumask, char* oldmask,
                          size_t mask_size) {
  return UV_ENOTSUP;
}

int uv_thread_setname(const char* name) {
  return UV_ENOTSUP;
}

int uv_thread_getname(uv_thread_t* tid, char* name, size_t size) {
  return UV_ENOTSUP;
}
