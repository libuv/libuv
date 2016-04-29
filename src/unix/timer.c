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
#include "internal.h"

#include <assert.h>
#include <limits.h>

int uv__init_timers(uv_loop_t* loop) {
  int j;
  struct uv__tvec_base *base = &loop->vec_base;

  for (j = 0; j < TVN_SIZE; j++) {
    QUEUE_INIT(base->tv5.vec + j);
    QUEUE_INIT(base->tv4.vec + j);
    QUEUE_INIT(base->tv3.vec + j);
    QUEUE_INIT(base->tv2.vec + j);
  }

  for (j = 0; j < TVR_SIZE; j++) {
    QUEUE_INIT(base->tv1.vec + j);
  }
  base->next_tick = loop->time;

  return 0;
}

static void uv__add_timer(struct uv__tvec_base *base, uv_timer_t *timer) {
  int i;
  unsigned long idx;
  unsigned long expires = timer->timeout;
  QUEUE *vec;
  idx = (timer->timeout - base->next_tick);

  if (timer->timeout < base->next_tick) {
     vec = base->tv1.vec + (base->next_tick & TVR_MASK);
     QUEUE_INSERT_TAIL(vec, &timer->timer_queue);
     return;
  }

  if (idx < TVR_SIZE) {
    i = expires & TVR_MASK;
    vec = base->tv1.vec + i;
  } else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
    i = (expires >> TVR_BITS) & TVN_MASK;
    vec = base->tv2.vec + i;
  } else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
    i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
    vec = base->tv3.vec + i;
  } else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
    i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
    vec = base->tv4.vec + i;
  } else {
     if (idx > MAX_TVAL) {
       idx = MAX_TVAL;
       expires = idx + base->next_tick;
     }
     i = (expires >> (TVR_BITS + 3*TVN_BITS)) & TVN_MASK;
     vec = base->tv5.vec + i;
  }

  QUEUE_INSERT_TAIL(vec, &timer->timer_queue);
}

static void uv__detach_timer(uv_timer_t *timer) {
  QUEUE_REMOVE(&timer->timer_queue);
}


static int cascade(struct uv__tvec_base *base, struct uv__tvec *tv, int index) {
  uv_timer_t *timer;
  QUEUE tv_list;
  QUEUE* q;

  QUEUE_MOVE(tv->vec + index, &tv_list);

  while (!QUEUE_EMPTY(&tv_list)) {
    q = QUEUE_HEAD(&tv_list);
    timer = QUEUE_DATA(q, uv_timer_t, timer_queue);
    QUEUE_REMOVE(q);
    uv__add_timer(base, timer);
  }

  return index;
}


int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_TIMER);
  handle->timer_cb = NULL;
  handle->repeat = 0;
  QUEUE_INIT(&handle->timer_queue);
  return 0;
}


int uv_timer_start(uv_timer_t* handle,
                   uv_timer_cb cb,
                   uint64_t timeout,
                   uint64_t repeat) {
  uint64_t clamped_timeout;

  if (cb == NULL)
    return -EINVAL;

  if (uv__is_active(handle))
    uv_timer_stop(handle);

  clamped_timeout = handle->loop->time + timeout;
  if (clamped_timeout < timeout) {
    clamped_timeout = (uint64_t) -1;
  }

  handle->timer_cb = cb;
  handle->timeout = clamped_timeout;
  handle->repeat = repeat;

  handle->loop->timer_counter++;
  uv__add_timer(&handle->loop->vec_base, handle);
  uv__handle_start(handle);

  return 0;
}


int uv_timer_stop(uv_timer_t* handle) {
  if (!uv__is_active(handle))
    return 0;

  handle->loop->timer_counter--;
  uv__detach_timer(handle);
  uv__handle_stop(handle);

  return 0;
}


int uv_timer_again(uv_timer_t* handle) {
  if (handle->timer_cb == NULL)
    return -EINVAL;

  if (handle->repeat) {
    uv_timer_stop(handle);
    uv_timer_start(handle, handle->timer_cb, handle->repeat, handle->repeat);
  }

  return 0;
}


void uv_timer_set_repeat(uv_timer_t* handle, uint64_t repeat) {
  handle->repeat = repeat;
}


uint64_t uv_timer_get_repeat(const uv_timer_t* handle) {
  return handle->repeat;
}


int uv__next_timeout(const uv_loop_t* loop) {
  const struct uv__tvec_base *base = &loop->vec_base;
  unsigned long expires = TVR_SIZE;
  int index, slot;
  uv_timer_t *nte;
  QUEUE* q;
  uint64_t diff;

  if (loop->timer_counter == 0)
    return -1; /* block indefinitely */

  /* Look for timer events in tv1. */
  index = slot = base->next_tick & TVR_MASK;
  do {
    QUEUE_FOREACH(q, base->tv1.vec + slot) {
    nte = QUEUE_DATA(q, uv_timer_t, timer_queue);
      /* Look at the cascade bucket(s)? */
      if (!index || slot < index) {
        break;
      }
      if (nte->timeout <= loop->time) {
        return 0;
      }

      diff = nte->timeout - loop->time;
      if (diff > INT_MAX)
        diff = INT_MAX;
      return diff;
    }
    slot = (slot + 1) & TVR_MASK;
  } while (slot != index);

  return expires;
}

#define INDEX(N)  ((base->next_tick >> (TVR_BITS + N * TVN_BITS)) & TVN_MASK)

void uv__run_timers(uv_loop_t* loop) {
  uv_timer_t* handle;
  uint64_t catchup = uv__hrtime(UV_CLOCK_FAST) / 1000000;
  struct uv__tvec_base *base = &loop->vec_base;
  QUEUE* q;

  while (base->next_tick <= catchup) {
    QUEUE work_list;
    QUEUE *head = &work_list;
    int index  = base->next_tick & TVR_MASK;

    if (!index &&
      (!cascade (base, &base->tv2, INDEX(0))) &&
      (!cascade (base, &base->tv3, INDEX(1))) &&
      (!cascade (base, &base->tv4, INDEX(2))))
          cascade (base, &base->tv5, INDEX(3));

    base->next_tick++;
    QUEUE_MOVE(base->tv1.vec + index, head);
    while (!QUEUE_EMPTY(head)) {
      q = QUEUE_HEAD(head);
      handle = QUEUE_DATA(q, uv_timer_t, timer_queue);
      assert(handle);
      uv_timer_stop(handle);
      uv_timer_again(handle);
      handle->timer_cb(handle);
    }
  }
}


void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
