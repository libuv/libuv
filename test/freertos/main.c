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

/* Smoke test for the FreeRTOS port. Exits with status 0 on success. Anything
 * else, including a hang, is a failure: run it with a timeout.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "uv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These mirror test/task.h, which can't be used here: it needs AF_INET6 and
 * PRId64, neither of which exist on newlib. Values are printed as longs because
 * newlib-nano's printf can't format 64-bit integers. That's lossless for error
 * codes and counters.
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define FAIL(...)                                                             \
  do {                                                                        \
    printf("%s:%d: assertion failed: ", __FILE__, __LINE__);                  \
    printf(__VA_ARGS__);                                                      \
    printf("\n");                                                             \
    finish(1);                                                                \
  } while (0)

#define ASSERT(expr)                                                          \
  do {                                                                        \
    if (!(expr))                                                              \
      FAIL("%s", #expr);                                                      \
  } while (0)

#define ASSERT_BASE(a, operator, b, type)                                     \
  do {                                                                        \
    type const eval_a = (a);                                                  \
    type const eval_b = (b);                                                  \
    if (!(eval_a operator eval_b))                                            \
      FAIL("`%s %s %s` (%ld %s %ld)",                                         \
           #a, #operator, #b, (long) eval_a, #operator, (long) eval_b);       \
  } while (0)

#define ASSERT_OK(a) ASSERT_BASE(a, ==, 0, int64_t)
#define ASSERT_EQ(a, b) ASSERT_BASE(a, ==, b, int64_t)
#define ASSERT_GT(a, b) ASSERT_BASE(a, >, b, int64_t)
#define ASSERT_UINT64_GE(a, b) ASSERT_BASE(a, >=, b, uint64_t)
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_PTR_EQ(a, b) ASSERT((a) == (b))

/* _Exit() doesn't flush stdio and output is lost when stdout is not a tty. */
static void finish(int status) {
  fflush(stdout);
  _Exit(status);
}

/* Real MCUs don't have megabytes of RAM to spare. */
#if defined(__arm__)
#define TASK_STACK_DEPTH (4 * 1024)
#else
#define TASK_STACK_DEPTH (256 * 1024)
#endif

void vApplicationGetIdleTaskMemory(StaticTask_t** ptask,
                                   StackType_t** pstack,
                                   configSTACK_DEPTH_TYPE* pstacksize) {
  static StackType_t stack[configMINIMAL_STACK_SIZE];
  static StaticTask_t task;

  *pstacksize = ARRAY_SIZE(stack);
  *pstack = stack;
  *ptask = &task;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** ptask,
                                    StackType_t** pstack,
                                    configSTACK_DEPTH_TYPE* pstacksize) {
  static StackType_t stack[configTIMER_TASK_STACK_DEPTH];
  static StaticTask_t task;

  *pstacksize = ARRAY_SIZE(stack);
  *pstack = stack;
  *ptask = &task;
}

static void* malloc_func(size_t size) {
  if (size == 0)
    return NULL;
  return pvPortMalloc(size);
}

static void* realloc_func(void* ptr, size_t size) {
  void* newptr;

  if (ptr == NULL)
    return malloc_func(size);
  if (size == 0) {
    vPortFree(ptr);
    return NULL;
  }
  newptr = malloc_func(size);
  if (newptr == NULL)
    return NULL;
  /* Assumes grow-only reallocation: heap_x.c doesn't expose the old size. */
  memcpy(newptr, ptr, size);
  vPortFree(ptr);
  return newptr;
}

static void* calloc_func(size_t count, size_t size) {
  size_t len;
  void* ptr;

  if (size != 0 && count > (size_t) -1 / size)
    return NULL;
  len = count * size;
  ptr = malloc_func(len);
  if (ptr != NULL)
    memset(ptr, 0, len);
  return ptr;
}

static void free_func(void* ptr) {
  if (ptr != NULL)
    vPortFree(ptr);
}

static int timer_count;
static int async_count;
static int close_count;

static void timer_cb(uv_timer_t* handle) {
  if (++timer_count == 3)
    uv_stop(handle->loop);
}

static void async_cb(uv_async_t* handle) {
  async_count++;
}

static void close_cb(uv_handle_t* handle) {
  ASSERT(uv_is_closing(handle));
  close_count++;
}

struct async_ctx {
  uv_cond_t cond;
  uv_mutex_t mutex;
  uv_async_t async;
  int done;
};

static void async_thread(void* arg) {
  struct async_ctx* ctx;
  int done;

  ctx = arg;
  do {
    vTaskDelay(pdMS_TO_TICKS(100));
    uv_async_send(&ctx->async);
    uv_mutex_lock(&ctx->mutex);
    done = ctx->done;
    if (done) {
      ctx->done = 2;
      uv_cond_signal(&ctx->cond);
    }
    uv_mutex_unlock(&ctx->mutex);
  } while (!done);
}

static void test_timer_and_async(void) {
  struct async_ctx ctx;
  uv_thread_t thread;
  uv_timer_t timer;
  uv_loop_t loop;
  uint64_t start;

  close_count = 0;
  memset(&ctx, 0, sizeof(ctx));
  ASSERT_OK(uv_mutex_init(&ctx.mutex));
  ASSERT_OK(uv_cond_init(&ctx.cond));
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_timer_init(&loop, &timer));
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 250, 250));
  ASSERT_OK(uv_async_init(&loop, &ctx.async, async_cb));
  ASSERT_OK(uv_thread_create(&thread, async_thread, &ctx));

  start = uv_hrtime();
  uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_EQ(timer_count, 3);
  ASSERT_GT(async_count, 0);
  /* Lower bound only, CI machines can be arbitrarily slow. */
  ASSERT_UINT64_GE(uv_hrtime() - start, 500 * 1000 * 1000ull);

  uv_mutex_lock(&ctx.mutex);
  ctx.done = 1;
  do
    uv_cond_wait(&ctx.cond, &ctx.mutex);
  while (ctx.done != 2);
  uv_mutex_unlock(&ctx.mutex);

  uv_close((uv_handle_t*) &ctx.async, close_cb);
  uv_close((uv_handle_t*) &timer, close_cb);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(close_count, 2);
  ASSERT_OK(uv_loop_close(&loop));
  ASSERT_OK(uv_thread_join(&thread));
  uv_mutex_destroy(&ctx.mutex);
  uv_cond_destroy(&ctx.cond);
}

static int idle_count;
static int prepare_count;
static int check_count;

static void idle_cb(uv_idle_t* handle) {
  if (++idle_count == 3)
    uv_stop(handle->loop);
}

static void prepare_cb(uv_prepare_t* handle) {
  prepare_count++;
}

static void check_cb(uv_check_t* handle) {
  check_count++;
}

static void test_loop_watchers(void) {
  uv_prepare_t prepare;
  uv_check_t check;
  uv_idle_t idle;
  uv_loop_t loop;

  close_count = 0;
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_idle_init(&loop, &idle));
  ASSERT_OK(uv_prepare_init(&loop, &prepare));
  ASSERT_OK(uv_check_init(&loop, &check));
  ASSERT_OK(uv_idle_start(&idle, idle_cb));
  ASSERT_OK(uv_prepare_start(&prepare, prepare_cb));
  ASSERT_OK(uv_check_start(&check, check_cb));
  uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_EQ(idle_count, 3);
  ASSERT_GT(prepare_count, 0);
  ASSERT_GT(check_count, 0);
  uv_close((uv_handle_t*) &idle, close_cb);
  uv_close((uv_handle_t*) &prepare, close_cb);
  uv_close((uv_handle_t*) &check, close_cb);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(close_count, 3);
  ASSERT_OK(uv_loop_close(&loop));
}

static int once_count;

static void once_cb(void) {
  once_count++;
}

static void test_once(void) {
  static uv_once_t once = UV_ONCE_INIT;

  uv_once(&once, once_cb);
  uv_once(&once, once_cb);
  ASSERT_EQ(once_count, 1);
}

static void test_locks(void) {
  uv_rwlock_t rwlock;
  uv_mutex_t mutex;
  uv_cond_t cond;
  uv_sem_t sem;

  ASSERT_OK(uv_sem_init(&sem, 1));
  ASSERT_OK(uv_sem_trywait(&sem));
  ASSERT_EQ(uv_sem_trywait(&sem), UV_EAGAIN);
  uv_sem_post(&sem);
  uv_sem_wait(&sem);
  uv_sem_destroy(&sem);

  ASSERT_OK(uv_mutex_init_recursive(&mutex));
  uv_mutex_lock(&mutex);
  uv_mutex_lock(&mutex);
  uv_mutex_unlock(&mutex);
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);

  ASSERT_OK(uv_rwlock_init(&rwlock));
  uv_rwlock_rdlock(&rwlock);
  ASSERT_OK(uv_rwlock_tryrdlock(&rwlock));
  ASSERT_EQ(uv_rwlock_trywrlock(&rwlock), UV_EBUSY);
  uv_rwlock_rdunlock(&rwlock);
  uv_rwlock_rdunlock(&rwlock);
  uv_rwlock_wrlock(&rwlock);
  ASSERT_EQ(uv_rwlock_tryrdlock(&rwlock), UV_EBUSY);
  uv_rwlock_wrunlock(&rwlock);
  uv_rwlock_destroy(&rwlock);

  ASSERT_OK(uv_mutex_init(&mutex));
  ASSERT_OK(uv_cond_init(&cond));
  uv_mutex_lock(&mutex);
  ASSERT_EQ(uv_cond_timedwait(&cond, &mutex, 10 * 1000 * 1000), UV_ETIMEDOUT);
  uv_mutex_unlock(&mutex);
  uv_cond_destroy(&cond);
  uv_mutex_destroy(&mutex);
}

static void barrier_thread(void* arg) {
  uv_barrier_wait(arg);
}

static void test_barrier(void) {
  uv_barrier_t barrier;
  uv_thread_t thread;

  ASSERT_OK(uv_barrier_init(&barrier, 2));
  ASSERT_OK(uv_thread_create(&thread, barrier_thread, &barrier));
  uv_barrier_wait(&barrier);
  ASSERT_OK(uv_thread_join(&thread));
  uv_barrier_destroy(&barrier);
}

static void test_key(void) {
#if configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0
  uv_key_t key;
  int value;

  ASSERT_OK(uv_key_create(&key));
  ASSERT_NULL(uv_key_get(&key));
  uv_key_set(&key, &value);
  ASSERT_PTR_EQ(uv_key_get(&key), &value);
  uv_key_delete(&key);
#else
  uv_key_t key;

  ASSERT_EQ(uv_key_create(&key), UV_ENOTSUP);
#endif
}

static void test_unsupported(void) {
  uv_loop_t loop;
  uv_tcp_t tcp;
  uv_fs_t req;

  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_EQ(uv_tcp_init(&loop, &tcp), UV_ENOTSUP);
  ASSERT_EQ(uv_fs_stat(&loop, &req, "/", NULL), UV_ENOTSUP);
  ASSERT_OK(uv_loop_close(&loop));
}

static void main_task(void* arg) {
  ASSERT_OK(uv_replace_allocator(malloc_func,
                                 realloc_func,
                                 calloc_func,
                                 free_func));
  test_once();
  test_locks();
  test_barrier();
  test_key();
  test_unsupported();
  test_loop_watchers();
  test_timer_and_async();
  printf("ok\n");
  finish(0);
}

int main(int argc, char** argv) {
  xTaskCreate(main_task, "uv", TASK_STACK_DEPTH, NULL, 3, NULL);
  vTaskStartScheduler();
  printf("vTaskStartScheduler returned\n");
  return 1;
}
