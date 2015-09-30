/* Copyright The libuv project and contributors. All rights reserved.
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

#include <string.h>


/*
 * The idea behind the test is as follows.
 * Certain handle types are stored in a queue internally.
 * Extra care should be taken for removal of a handle from the queue while iterating over the queue.
 * (i.e., QUEUE_REMOVE() called within QUEUE_FOREACH())
 * This usually happens when someone closes or stops a handle from within its callback.
 * So we need to check that we haven't screwed the queue on close/stop.
 * To do so we do the following (for each handle type):
 *  1. Create and start 3 handles (#0, #1, and #2).
 *
 *     The queue after the start() calls:
 *     ..=> [queue head] <=> [handle] <=> [handle #1] <=> [handle] <=..
 *
 *  2. Trigger handles to fire (for uv_idle_t, uv_prepare_t, and uv_check_t there is nothing to do).
 *
 *  3. In the callback for the first-executed handle (#0 or #2 depending on handle type)
 *     stop the handle and the next one (#1).
 *     (for uv_idle_t, uv_prepare_t, and uv_check_t callbacks are executed in the reverse order as they are start()'ed,
 *     so callback for handle #2 will be called first)
 *
 *     The queue after the stop() calls:
 *                                correct foreach "next"  |
 *                                                       \/
 *     ..=> [queue head] <==============================> [handle] <=..
 *          [          ] <-  [handle] <=> [handle #1]  -> [      ]
 *                                       /\
 *                  wrong foreach "next"  |
 *
 *  4. The callback for handle #1 shouldn't be called because the handle #1 is stopped in the previous step.
 *     However, if QUEUE_REMOVE() is not handled properly within QUEUE_FOREACH(), the callback _will_ be called.
 */

static const unsigned first_handle_number_idle     = 2;
static const unsigned first_handle_number_prepare  = 2;
static const unsigned first_handle_number_check    = 2;


#define DEFINE_GLOBALS_AND_CBS(name)                                          \
  static uv_##name##_t (name)[3];                                             \
  static unsigned name##_cb_calls[3];                                         \
                                                                              \
  static void name##2_cb(uv_##name##_t* handle) {                             \
    ASSERT(handle == &(name)[2]);                                             \
    if (first_handle_number_##name == 2) {                                    \
      uv_close((uv_handle_t*)&(name)[2], NULL);                               \
      uv_close((uv_handle_t*)&(name)[1], NULL);                               \
    }                                                                         \
    name##_cb_calls[2]++;                                                     \
  }                                                                           \
                                                                              \
  static void name##1_cb(uv_##name##_t* handle) {                             \
    ASSERT(handle == &(name)[1]);                                             \
    ASSERT(0 && "Shouldn't be called" && (&name[0]));                         \
  }                                                                           \
                                                                              \
  static void name##0_cb(uv_##name##_t* handle) {                             \
    ASSERT(handle == &(name)[0]);                                             \
    if (first_handle_number_##name == 0) {                                    \
      uv_close((uv_handle_t*)&(name)[0], NULL);                               \
      uv_close((uv_handle_t*)&(name)[1], NULL);                               \
    }                                                                         \
    name##_cb_calls[0]++;                                                     \
  }                                                                           \
                                                                              \
  static const uv_##name##_cb name##_cbs[] = {                                \
    (uv_##name##_cb)name##0_cb,                                               \
    (uv_##name##_cb)name##1_cb,                                               \
    (uv_##name##_cb)name##2_cb,                                               \
  };

#define INIT_AND_START(name, loop)                                            \
  do {                                                                        \
    size_t i;                                                                 \
    for (i = 0; i < ARRAY_SIZE(name); i++) {                                  \
      int r;                                                                  \
      r = uv_##name##_init((loop), &(name)[i]);                               \
      ASSERT(r == 0);                                                         \
                                                                              \
      r = uv_##name##_start(&(name)[i], name##_cbs[i]);                       \
      ASSERT(r == 0);                                                         \
    }                                                                         \
  } while (0)

#define END_ASSERTS(name)                                                     \
  do {                                                                        \
    ASSERT(name##_cb_calls[0] == 1);                                          \
    ASSERT(name##_cb_calls[1] == 0);                                          \
    ASSERT(name##_cb_calls[2] == 1);                                          \
  } while (0)

DEFINE_GLOBALS_AND_CBS(idle)
DEFINE_GLOBALS_AND_CBS(prepare)
DEFINE_GLOBALS_AND_CBS(check)


TEST_IMPL(queue_foreach_delete) {
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();

  INIT_AND_START(idle,    loop);
  INIT_AND_START(prepare, loop);
  INIT_AND_START(check,   loop);

  r = uv_run(loop, UV_RUN_NOWAIT);
  ASSERT(r == 1);

  END_ASSERTS(idle);
  END_ASSERTS(prepare);
  END_ASSERTS(check);

  MAKE_VALGRIND_HAPPY();

  return 0;
}
