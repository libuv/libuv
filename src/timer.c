/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#include <limits.h>


static struct heap *timer_heap(const uv_loop_t* loop) {
#ifdef _WIN32
  return (struct heap*) loop->timer_heap;
#else
  return (struct heap*) &loop->timer_heap;
#endif
}


static int timer_less_than(const struct heap_node* ha,
                           const struct heap_node* hb) {
  const uv_timer_t* a;
  const uv_timer_t* b;

  a = container_of(ha, uv_timer_t, node.heap);
  b = container_of(hb, uv_timer_t, node.heap);

  if (a->timeout < b->timeout)
    return 1;
  if (b->timeout < a->timeout)
    return 0;

  /* Compare start_id when both have the same timeout. start_id is
   * allocated with loop->timer_counter in uv_timer_start().
   */
  return a->start_id < b->start_id;
}


int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_TIMER);
  handle->timer_cb = NULL;
  handle->timeout = 0;
  handle->repeat = 0;
  uv__queue_init(&handle->node.queue);
  return 0;
}


static int uv__timer_start(uv_timer_t* handle,
                           uv_timer_cb cb,
                           uint64_t timeout,
                           uint64_t repeat) {
  uint64_t clamped_timeout;

  if (uv__is_closing(handle) || cb == NULL)
    return UV_EINVAL;

  uv_timer_stop(handle);

  clamped_timeout = handle->loop->time + timeout;
  if (clamped_timeout < timeout)
    clamped_timeout = (uint64_t) -1;

  handle->timer_cb = cb;
  handle->timeout = clamped_timeout;
  handle->repeat = repeat;
  /* start_id is the second index to be compared in timer_less_than() */
  handle->start_id = handle->loop->timer_counter++;

  heap_insert(timer_heap(handle->loop),
              (struct heap_node*) &handle->node.heap,
              timer_less_than);
  uv__handle_start(handle);

  return 0;
}


static uint64_t uv__ms_to_ns_clamp(uint64_t ms) {
  if (ms > (uint64_t) -1 / (1000 * 1000))
    return (uint64_t) -1;
  return ms * (1000 * 1000);
}


int uv_timer_start(uv_timer_t* handle,
                   uv_timer_cb cb,
                   uint64_t timeout,
                   uint64_t repeat) {
  timeout = uv__ms_to_ns_clamp(timeout);
  repeat = uv__ms_to_ns_clamp(repeat);
  return uv__timer_start(handle, cb, timeout, repeat);
}


int uv_timer_stop(uv_timer_t* handle) {
  if (uv__is_active(handle)) {
    heap_remove(timer_heap(handle->loop),
                (struct heap_node*) &handle->node.heap,
                timer_less_than);
    uv__handle_stop(handle);
  } else {
    uv__queue_remove(&handle->node.queue);
  }

  uv__queue_init(&handle->node.queue);
  return 0;
}


int uv_timer_again(uv_timer_t* handle) {
  if (handle->timer_cb == NULL)
    return UV_EINVAL;

  if (handle->repeat) {
    uv_timer_stop(handle);
    uv__timer_start(handle, handle->timer_cb, handle->repeat, handle->repeat);
  }

  return 0;
}


void uv_timer_set_repeat(uv_timer_t* handle, uint64_t repeat) {
  handle->repeat = uv__ms_to_ns_clamp(repeat);
}


uint64_t uv_timer_get_repeat(const uv_timer_t* handle) {
  return uv__ns_to_ms(handle->repeat);
}


uint64_t uv_timer_get_due_in(const uv_timer_t* handle) {
  if (handle->loop->time >= handle->timeout)
    return 0;

  if (handle->timeout == (uint64_t) -1)
    return -1;

  return uv__ns_to_ms(handle->timeout - handle->loop->time);
}


uint64_t uv__next_timeout(const uv_loop_t* loop) {
  const struct heap_node* heap_node;
  const uv_timer_t* handle;

  heap_node = heap_min(timer_heap(loop));
  if (heap_node == NULL)
    return -1; /* block indefinitely */

  handle = container_of(heap_node, uv_timer_t, node.heap);
  if (handle->timeout <= loop->time)
    return 0;

  return handle->timeout - loop->time;
}


void uv__run_timers(uv_loop_t* loop) {
  struct heap_node* heap_node;
  uv_timer_t* handle;
  struct uv__queue* queue_node;
  struct uv__queue ready_queue;

  uv__queue_init(&ready_queue);

  for (;;) {
    heap_node = heap_min(timer_heap(loop));
    if (heap_node == NULL)
      break;

    handle = container_of(heap_node, uv_timer_t, node.heap);
    if (handle->timeout > loop->time)
      break;

    uv_timer_stop(handle);
    uv__queue_insert_tail(&ready_queue, &handle->node.queue);
  }

  while (!uv__queue_empty(&ready_queue)) {
    queue_node = uv__queue_head(&ready_queue);
    uv__queue_remove(queue_node);
    uv__queue_init(queue_node);
    handle = container_of(queue_node, uv_timer_t, node.queue);

    uv_timer_again(handle);
    handle->timer_cb(handle);
  }
}


void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}


uint64_t uv__ns_to_ms(uint64_t ns) {
  uint64_t frac;
  uint64_t ms;

  if (ns == (uint64_t) -1)
    return (uint64_t) -1;
  frac = ns % (1000 * 1000);
  ms = ns / (1000 * 1000);
  if (frac > 500 * 1000)
    ms += 1;
  else if (frac == 500 * 1000)
    ms += ms & 1;  /* Bankers' rounding, averages out exact .5 midpoints. */
  return ms;
}


int uv__ns_to_ms_sat(uint64_t ns) {
  uint64_t ms;

  ms = uv__ns_to_ms(ns);
  if (ms == (uint64_t) -1)
    return -1;
  if (ms > INT_MAX)
    return INT_MAX;
  return ms;
}
