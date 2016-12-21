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

/* Tests commented out with XXX are ones that are failing on Linux */

/*
 * Purpose of this test is to check semantics of starting and stopping
 * prepare, check and spin watchers.
 *
 * - A watcher must be able to safely stop or close itself;
 * - Once a watcher is stopped or closed its callback should never be called.
 * - If a watcher is closed, it is implicitly stopped and its close_cb should
 *   be called exactly once.
 * - A watcher can safely start and stop other watchers of the same type.
 * - Prepare and check watchers are called once per event loop iterations.
 * - All active spin watchers are queued when the event loop has no more work
 *   to do. This is done repeatedly until all spin watchers are inactive.
 * - If a watcher starts another watcher of the same type its callback is not
 *   immediately queued. For check and prepare watchers, that means that if
 *   a watcher makes another of the same type active, it'll not be called until
 *   the next event loop iteration. For spin watchers this means that the
 *   newly activated spin watcher might not be queued immediately.
 * - Prepare, check, spin watchers keep the event loop alive even when they're
 *   not active.
 *
 * This is what the test globally does:
 *
 * - prepare_1 is always active and counts event loop iterations. It also
 *   creates and starts prepare_2 every other iteration. Finally it verifies
 *   that no spin watchers are active before polling.
 * - prepare_2 is started by prepare_1 every other iteration. It immediately
 *   stops itself. It verifies that a watcher is not queued immediately
 *   if created by another watcher of the same type.
 * - There's a check watcher that stops the event loop after a certain number
 *   of iterations. It starts a varying number of spin_1 watchers.
 * - Idle_1 watchers stop themselves after being called a few times. All spin_1
 *   watchers try to start the spin_2 watcher if it is not already started or
 *   awaiting its close callback.
 * - The spin_2 watcher always exists but immediately closes itself after
 *   being started by a check_1 watcher. It verifies that a watcher is
 *   implicitly stopped when closed, and that a watcher can close itself
 *   safely.
 * - There is a repeating timer. It does not keep the event loop alive
 *   (ev_unref) but makes sure that the loop keeps polling the system for
 *   events.
 */


#include "uv.h"
#include "task.h"

#include <math.h>


#define IDLE_COUNT      7
#define ITERATIONS      21
#define TIMEOUT         100


static uv_prepare_t prepare_1_handle;
static uv_prepare_t prepare_2_handle;

static uv_check_t check_handle;

static uv_spin_t spin_1_handles[IDLE_COUNT];
static uv_spin_t spin_2_handle;

static uv_timer_t timer_handle;


static int loop_iteration = 0;

static int prepare_1_cb_called = 0;
static int prepare_1_close_cb_called = 0;

static int prepare_2_cb_called = 0;
static int prepare_2_close_cb_called = 0;

static int check_cb_called = 0;
static int check_close_cb_called = 0;

static int spin_1_cb_called = 0;
static int spin_1_close_cb_called = 0;
static int spins_1_active = 0;

static int spin_2_cb_called = 0;
static int spin_2_close_cb_called = 0;
static int spin_2_cb_started = 0;
static int spin_2_is_active = 0;


static void timer_cb(uv_timer_t* handle) {
  ASSERT(handle == &timer_handle);
}


static void spin_2_close_cb(uv_handle_t *handle) {
  fprintf(stderr, "%s", "IDLE_2_CLOSE_CB\n");
  fflush(stderr);

  ASSERT(handle == (uv_handle_t*)&spin_2_handle);

  ASSERT(spin_2_is_active);

  spin_2_close_cb_called++;
  spin_2_is_active = 0;
}


static void spin_2_cb(uv_spin_t* handle) {
  fprintf(stderr, "%s", "IDLE_2_CB\n");
  fflush(stderr);

  ASSERT(handle == &spin_2_handle);

  spin_2_cb_called++;

  uv_close((uv_handle_t *) handle, spin_2_close_cb);
}


static void spin_1_cb(uv_spin_t* handle) {
  int r;

  fprintf(stderr, "%s", "IDLE_1_CB\n");
  fflush(stderr);

  ASSERT(handle != NULL);
  ASSERT(spins_1_active > 0);

  /* Init spin_2 and make it active */
  if (!spin_2_is_active && !uv_is_closing((uv_handle_t*)&spin_2_handle)) {
    r = uv_spin_init(uv_default_loop(), &spin_2_handle);
    ASSERT(r == 0);
    r = uv_spin_start(&spin_2_handle, spin_2_cb);
    ASSERT(r == 0);
    spin_2_is_active = 1;
    spin_2_cb_started++;
  }

  spin_1_cb_called++;

  if (spin_1_cb_called % 5 == 0) {
    r = uv_spin_stop((uv_spin_t*)handle);
    ASSERT(r == 0);
    spins_1_active--;
  }
}


static void spin_1_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "%s", "IDLE_1_CLOSE_CB\n");
  fflush(stderr);

  ASSERT(handle != NULL);

  spin_1_close_cb_called++;
}


static void prepare_1_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "%s", "PREPARE_1_CLOSE_CB");
  fflush(stderr);
  ASSERT(handle == (uv_handle_t*)&prepare_1_handle);

  prepare_1_close_cb_called++;
}


static void check_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "%s", "CHECK_CLOSE_CB\n");
  fflush(stderr);
  ASSERT(handle == (uv_handle_t*)&check_handle);

  check_close_cb_called++;
}


static void prepare_2_close_cb(uv_handle_t* handle) {
  fprintf(stderr, "%s", "PREPARE_2_CLOSE_CB\n");
  fflush(stderr);
  ASSERT(handle == (uv_handle_t*)&prepare_2_handle);

  prepare_2_close_cb_called++;
}


static void check_cb(uv_check_t* handle) {
  int i, r;

  fprintf(stderr, "%s", "CHECK_CB\n");
  fflush(stderr);
  ASSERT(handle == &check_handle);

  if (loop_iteration < ITERATIONS) {
    /* Make some spin watchers active */
    for (i = 0; i < 1 + (loop_iteration % IDLE_COUNT); i++) {
      r = uv_spin_start(&spin_1_handles[i], spin_1_cb);
      ASSERT(r == 0);
      spins_1_active++;
    }

  } else {
    /* End of the test - close all handles */
    uv_close((uv_handle_t*)&prepare_1_handle, prepare_1_close_cb);
    uv_close((uv_handle_t*)&check_handle, check_close_cb);
    uv_close((uv_handle_t*)&prepare_2_handle, prepare_2_close_cb);

    for (i = 0; i < IDLE_COUNT; i++) {
      uv_close((uv_handle_t*)&spin_1_handles[i], spin_1_close_cb);
    }

    /* This handle is closed/recreated every time, close it only if it is */
    /* active.*/
    if (spin_2_is_active) {
      uv_close((uv_handle_t *) &spin_2_handle, spin_2_close_cb);
    }
  }

  check_cb_called++;
}


static void prepare_2_cb(uv_prepare_t* handle) {
  int r;

  fprintf(stderr, "%s", "PREPARE_2_CB\n");
  fflush(stderr);
  ASSERT(handle == &prepare_2_handle);

  /* prepare_2 gets started by prepare_1 when (loop_iteration % 2 == 0), */
  /* and it stops itself immediately. A started watcher is not queued */
  /* until the next round, so when this callback is made */
  /* (loop_iteration % 2 == 0) cannot be true. */
  ASSERT(loop_iteration % 2 != 0);

  r = uv_prepare_stop((uv_prepare_t*)handle);
  ASSERT(r == 0);

  prepare_2_cb_called++;
}


static void prepare_1_cb(uv_prepare_t* handle) {
  int r;

  fprintf(stderr, "%s", "PREPARE_1_CB\n");
  fflush(stderr);
  ASSERT(handle == &prepare_1_handle);

  if (loop_iteration % 2 == 0) {
    r = uv_prepare_start(&prepare_2_handle, prepare_2_cb);
    ASSERT(r == 0);
  }

  prepare_1_cb_called++;
  loop_iteration++;

  printf("Loop iteration %d of %d.\n", loop_iteration, ITERATIONS);
}


TEST_IMPL(loop_handles) {
  int i;
  int r;

  r = uv_prepare_init(uv_default_loop(), &prepare_1_handle);
  ASSERT(r == 0);
  r = uv_prepare_start(&prepare_1_handle, prepare_1_cb);
  ASSERT(r == 0);

  r = uv_check_init(uv_default_loop(), &check_handle);
  ASSERT(r == 0);
  r = uv_check_start(&check_handle, check_cb);
  ASSERT(r == 0);

  /* initialize only, prepare_2 is started by prepare_1_cb */
  r = uv_prepare_init(uv_default_loop(), &prepare_2_handle);
  ASSERT(r == 0);

  for (i = 0; i < IDLE_COUNT; i++) {
    /* initialize only, spin_1 handles are started by check_cb */
    r = uv_spin_init(uv_default_loop(), &spin_1_handles[i]);
    ASSERT(r == 0);
  }

  /* don't init or start spin_2, both is done by spin_1_cb */

  /* the timer callback is there to keep the event loop polling */
  /* unref it as it is not supposed to keep the loop alive */
  r = uv_timer_init(uv_default_loop(), &timer_handle);
  ASSERT(r == 0);
  r = uv_timer_start(&timer_handle, timer_cb, TIMEOUT, TIMEOUT);
  ASSERT(r == 0);
  uv_unref((uv_handle_t*)&timer_handle);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(r == 0);

  ASSERT(loop_iteration == ITERATIONS);

  ASSERT(prepare_1_cb_called == ITERATIONS);
  ASSERT(prepare_1_close_cb_called == 1);

  ASSERT(prepare_2_cb_called == floor(ITERATIONS / 2.0));
  ASSERT(prepare_2_close_cb_called == 1);

  ASSERT(check_cb_called == ITERATIONS);
  ASSERT(check_close_cb_called == 1);

  /* spin_1_cb should be called a lot */
  ASSERT(spin_1_close_cb_called == IDLE_COUNT);

  ASSERT(spin_2_close_cb_called == spin_2_cb_started);
  ASSERT(spin_2_is_active == 0);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
