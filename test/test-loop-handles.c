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

/*
 * Purpose of this test is to check semantics of starting and stopping
 * prepare, check and idle watchers.
 *
 * - A watcher must be able to safely stop or close itself;
 * - Once a watcher is stopped or closed its callback should never be called.
 * - If a watcher is closed, it is implicitly stopped and its close_cb should
 *   be called exactly once.
 * - A watcher can safely start and stop other watchers of the same type.
 * - Prepare and check watchers are called once per event loop iterations.
 * - All active idle watchers are queued when the event loop has no more work
 *   to do. This is done repeatedly until all idle watchers are inactive.
 * - If a watcher starts another watcher of the same type its callback is not
 *   immediately queued. For check and prepare watchers, that means that if
 *   a watcher makes another of the same type active, it'll not be called until
 *   the next event loop iteration. For idle. watchers this means that the
 *   newly activated idle watcher might not be queued immediately.
 * - Prepare, check, idle watchers keep the event loop alive even when they're
 *   not active.
 *
 * This is what the test globally does:
 *
 * - prepare_1 is always active and counts event loop iterations. It also
 *   creates and starts prepare_2 every other iteration. Finally it verifies
 *   that no idle watchers are active before polling.
 * - prepare_2 is started by prepare_1 every other iteration. It immediately
 *   stops itself. It verifies that a watcher is not queued immediately
 *   if created by another watcher of the same type.
 * - There's a check watcher that stops the event loop after a certain number
 *   of iterations. It starts a varying number of idle_1 watchers.
 * - Idle_1 watchers stop themselves after being called a few times. All idle_1
 *   watchers try to start the idle_2 watcher if it is not already started or
 *   awaiting its close callback.
 * - The idle_2 watcher always exists but immediately closes itself after
 *   being started by a check_1 watcher. It verifies that a watcher is
 *   implicitly stopped when closed, and that a watcher can close itself
 *   safely.
 * - There is a timeout request that reposts itself after every timeout. It
 *   does not keep te event loop alive (ev_unref) but makes sure that the loop
 *   keeps polling the system for events.
 */

#include "../oio.h"
#include "task.h"

#include <math.h>


#define IDLE_COUNT      7
#define ITERATIONS      21
#define TIMEOUT         100


static oio_handle prepare_1_handle;
static oio_handle prepare_2_handle;

static oio_handle check_handle;

static oio_handle idle_1_handles[IDLE_COUNT];
static oio_handle idle_2_handle;

static oio_req timeout_req;


static int loop_iteration = 0;

static int prepare_1_cb_called = 0;
static int prepare_1_close_cb_called = 0;

static int prepare_2_cb_called = 0;
static int prepare_2_close_cb_called = 0;

static int check_cb_called = 0;
static int check_close_cb_called = 0;

static int idle_1_cb_called = 0;
static int idle_1_close_cb_called = 0;
static int idles_1_active = 0;

static int idle_2_cb_called = 0;
static int idle_2_close_cb_called = 0;
static int idle_2_cb_started = 0;
static int idle_2_is_active = 0;

static int timeout_cb_called = 0;


static void timeout_cb(oio_req *req, int64_t skew, int status) {
  ASSERT(req == &timeout_req);
  ASSERT(status == 0);

  timeout_cb_called++;

  oio_timeout(req, TIMEOUT);
}


static void idle_2_cb(oio_handle* handle, int status) {
  ASSERT(handle == &idle_2_handle);
  ASSERT(status == 0);

  idle_2_cb_called++;

  oio_close(handle);
}


static void idle_2_close_cb(oio_handle* handle, int status){
  ASSERT(handle == &idle_2_handle);
  ASSERT(status == 0);

  ASSERT(idle_2_is_active);

  idle_2_close_cb_called++;
  idle_2_is_active = 0;
}


static void idle_1_cb(oio_handle* handle, int status) {
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  ASSERT(idles_1_active > 0);

  /* Init idle_2 and make it active */
  if (!idle_2_is_active) {
    oio_idle_init(&idle_2_handle, idle_2_close_cb, NULL);
    oio_idle_start(&idle_2_handle, idle_2_cb);
    idle_2_is_active = 1;
    idle_2_cb_started++;
  }

  idle_1_cb_called++;

  if (idle_1_cb_called % 5 == 0) {
    oio_idle_stop(handle);
    idles_1_active--;
  }
}


static void idle_1_close_cb(oio_handle* handle, int status){
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  idle_1_close_cb_called++;
}


static void check_cb(oio_handle* handle, int status) {
  int i;

  ASSERT(handle == &check_handle);
  ASSERT(status == 0);

  ASSERT(idles_1_active == 0);
  ASSERT(idle_2_is_active == 0);

  if (loop_iteration < ITERATIONS) {
    /* Make some idle watchers active */
    for (i = 0; i < 1 + (loop_iteration % IDLE_COUNT); i++) {
      oio_idle_start(&idle_1_handles[i], idle_1_cb);
      idles_1_active++;
    }

  } else {
    /* End of the test - close all handles */
    oio_close(&prepare_1_handle);
    oio_close(&check_handle);
    oio_close(&prepare_2_handle);

    for (i = 0; i < IDLE_COUNT; i++) {
      oio_close(&idle_1_handles[i]);
    }

    /* This handle is closed/recreated every time, close it only if it is */
    /* active.*/
    if (idle_2_is_active) {
      oio_close(&idle_2_handle);
    }
  }

  check_cb_called++;
}


static void check_close_cb(oio_handle* handle, int status){
  ASSERT(handle == &check_handle);
  ASSERT(status == 0);

  check_close_cb_called++;
}


static void prepare_2_cb(oio_handle* handle, int status) {
  ASSERT(handle == &prepare_2_handle);
  ASSERT(status == 0);

  ASSERT(idles_1_active == 0);
  ASSERT(idle_2_is_active == 0);

  /* prepare_2 gets started by prepare_1 when (loop_iteration % 2 == 0), */
  /* and it stops itself immediately. A started watcher is not queued */
  /* until the next round, so when this callback is made */
  /* (loop_iteration % 2 == 0) cannot be true. */
  ASSERT(loop_iteration % 2 != 0);

  oio_prepare_stop(handle);

  prepare_2_cb_called++;
}


static void prepare_2_close_cb(oio_handle* handle, int status){
  ASSERT(handle == &prepare_2_handle);
  ASSERT(status == 0);

  prepare_2_close_cb_called++;
}


static void prepare_1_cb(oio_handle* handle, int status) {
  ASSERT(handle == &prepare_1_handle);
  ASSERT(status == 0);

  ASSERT(idles_1_active == 0);
  ASSERT(idle_2_is_active == 0);

  if (loop_iteration % 2 == 0) {
    oio_prepare_start(&prepare_2_handle, prepare_2_cb);
  }

  prepare_1_cb_called++;
  loop_iteration++;

  printf("Loop iteration %d of %d.\n", loop_iteration, ITERATIONS);
}


static void prepare_1_close_cb(oio_handle* handle, int status){
  ASSERT(handle == &prepare_1_handle);
  ASSERT(status == 0);

  prepare_1_close_cb_called++;
}


static oio_buf alloc_cb(oio_handle* handle, size_t size) {
  oio_buf rv = { 0, 0 };
  FATAL("alloc_cb should never be called in this test");
  return rv;
}


TEST_IMPL(loop_handles) {
  int i;

  oio_init(alloc_cb);

  oio_prepare_init(&prepare_1_handle, prepare_1_close_cb, NULL);
  oio_prepare_start(&prepare_1_handle, prepare_1_cb);

  oio_check_init(&check_handle, check_close_cb, NULL);
  oio_check_start(&check_handle, check_cb);

  /* initialize only, prepare_2 is started by prepare_1_cb */
  oio_prepare_init(&prepare_2_handle, prepare_2_close_cb, NULL);

  for (i = 0; i < IDLE_COUNT; i++) {
    /* initialize only, idle_1 handles are started by check_cb */
    oio_idle_init(&idle_1_handles[i], idle_1_close_cb, NULL);
  }

  /* don't init or start idle_2, both is done by idle_1_cb */

  /* the timer callback is there to keep the event loop polling */
  /* unref it as it is not supposed to keep the loop alive */
  oio_req_init(&timeout_req, NULL, timeout_cb);
  oio_timeout(&timeout_req, TIMEOUT);
  oio_unref();

  oio_run();

  ASSERT(loop_iteration == ITERATIONS);

  ASSERT(prepare_1_cb_called == ITERATIONS);
  ASSERT(prepare_1_close_cb_called == 1);

  ASSERT(prepare_2_cb_called == floor(ITERATIONS / 2));
  ASSERT(prepare_2_close_cb_called == 1);

  ASSERT(check_cb_called == ITERATIONS);
  ASSERT(check_close_cb_called == 1);

  /* idle_1_cb should be called a lot */
  ASSERT(idle_1_cb_called >= ITERATIONS * IDLE_COUNT * 2);
  ASSERT(idle_1_close_cb_called == IDLE_COUNT);
  ASSERT(idles_1_active == 0);

  ASSERT(idle_2_cb_started >= ITERATIONS);
  ASSERT(idle_2_cb_called == idle_2_cb_started);
  ASSERT(idle_2_close_cb_called == idle_2_cb_started);
  ASSERT(idle_2_is_active == 0);

  ASSERT(timeout_cb_called > 0);

  return 0;
}
