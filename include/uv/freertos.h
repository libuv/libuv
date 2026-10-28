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

#ifndef UV_FREERTOS_H
#define UV_FREERTOS_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "timers.h"

#include "uv/threadpool.h"

typedef struct uv_buf_t {
  char* base;
  unsigned int len;
} uv_buf_t;

typedef int uv_file;
typedef int uv_os_sock_t;
typedef int uv_os_fd_t;
typedef int uv_pid_t;
typedef int uv_gid_t;
typedef int uv_uid_t;

#define UV_ONCE_INIT {0}

typedef struct uv_barrier_s uv_barrier_t;
typedef struct uv_cond_s uv_cond_t;
typedef struct uv_key_s uv_key_t;
typedef struct uv_mutex_s uv_mutex_t;
typedef struct uv_once_s uv_once_t;
typedef struct uv_rwlock_s uv_rwlock_t;
typedef struct uv_sem_s uv_sem_t;
typedef struct uv_thread_s uv_thread_t;

typedef struct uv_lib_s {} uv_lib_t;
typedef struct {} uv__dirent_t;

struct uv_once_s {
  volatile uint32_t flag;
};

struct uv_sem_s {
  SemaphoreHandle_t sema;
#if configSUPPORT_STATIC_ALLOCATION == 1
  StaticSemaphore_t sema_buf;
#endif
};

struct uv_mutex_s {
  SemaphoreHandle_t sema;
  int recursive;
#if configSUPPORT_STATIC_ALLOCATION == 1
  StaticSemaphore_t sema_buf;
#endif
};

struct uv_rwlock_s {
  uv_mutex_t mutex;
  uv_sem_t sem;
  uint32_t count;
};

struct uv_cond_s {
  uv_mutex_t mutex;
  struct uv__queue queue;
};

struct uv_barrier_s {
  uv_mutex_t mutex;
  uv_cond_t cond;
  unsigned threshold;
  unsigned out;
  unsigned in;
};

struct uv_thread_s {
  TaskHandle_t task;
  void* ctx;
};

struct uv_key_s {
  uint32_t idx;
};

struct uv_addrinfo_s {
};

struct uv_sockaddr_s {
  unsigned short sa_family;
};

struct uv_sockaddr_in_s {
  unsigned short sin_family;
  unsigned short sin_port;
  struct {
    unsigned long s_addr;
  } sin_addr;
};

struct uv_sockaddr_in6_s {
  unsigned short sin6_family;
  unsigned short sin6_port;
  unsigned short sin6_scope_id;
  unsigned long sin6_addr;
};

#define UV_CONNECT_PRIVATE_FIELDS
#define UV_DIR_PRIVATE_FIELDS
#define UV_FS_EVENT_PRIVATE_FIELDS
#define UV_FS_PRIVATE_FIELDS
#define UV_GETADDRINFO_PRIVATE_FIELDS
#define UV_GETNAMEINFO_PRIVATE_FIELDS
#define UV_PIPE_PRIVATE_FIELDS
#define UV_POLL_PRIVATE_FIELDS
#define UV_PRIVATE_REQ_TYPES
#define UV_PROCESS_PRIVATE_FIELDS
#define UV_REQ_PRIVATE_FIELDS
#define UV_REQ_TYPE_PRIVATE
#define UV_SHUTDOWN_PRIVATE_FIELDS
#define UV_SIGNAL_PRIVATE_FIELDS
#define UV_TCP_PRIVATE_FIELDS
#define UV_TTY_PRIVATE_FIELDS
#define UV_UDP_PRIVATE_FIELDS
#define UV_UDP_SEND_PRIVATE_FIELDS
#define UV_WORK_PRIVATE_FIELDS
#define UV_WRITE_PRIVATE_FIELDS
#define UV_STREAM_PRIVATE_FIELDS

#define UV_ASYNC_PRIVATE_FIELDS                                               \
  uv_async_cb async_cb;                                                       \
  struct uv__queue queue;                                                     \
  int pending;

#define UV_CHECK_PRIVATE_FIELDS                                               \
  uv_check_cb check_cb;                                                       \
  struct uv__queue queue;                                                     \

#define UV_HANDLE_PRIVATE_FIELDS                                              \
  uv_handle_t* next_closing;                                                  \
  int flags;                                                                  \

#define UV_IDLE_PRIVATE_FIELDS                                                \
  uv_idle_cb idle_cb;                                                         \
  struct uv__queue queue;                                                     \

#define UV_LOOP_PRIVATE_FIELDS                                                \
  uv_handle_t* closing_handles;                                               \
  struct uv__queue async_handles;                                             \
  struct uv__queue check_handles;                                             \
  struct uv__queue idle_handles;                                              \
  struct uv__queue pending_queue;                                             \
  struct uv__queue prepare_handles;                                           \
  struct {                                                                    \
    void* min;                                                                \
    unsigned int nelts;                                                       \
  } timer_heap;                                                               \
  uint64_t timer_counter;                                                     \
  uint64_t time;                                                              \
  TaskHandle_t current_task;                                                  \

#define UV_PREPARE_PRIVATE_FIELDS                                             \
  uv_prepare_cb prepare_cb;                                                   \
  struct uv__queue queue;                                                     \


#define UV_TIMER_PRIVATE_FIELDS                                               \
  uv_timer_cb timer_cb;                                                       \
  union {                                                                     \
    void* heap[3];                                                            \
    struct uv__queue queue;                                                   \
  } node;                                                                     \
  uint64_t timeout;                                                           \
  uint64_t repeat;                                                            \
  uint64_t start_id;                                                          \

#endif  /* UV_FREERTOS_H */
