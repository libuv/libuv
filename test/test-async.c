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

#include "../oio.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>


static oio_handle_t prepare_handle;

static oio_handle_t async1_handle;
static oio_handle_t async2_handle;

static int prepare_cb_called = 0;

static int async1_cb_called = 0;
static int async2_cb_called = 0;

static int close_cb_called = 0;

static uintptr_t thread1_id = 0;
static uintptr_t thread2_id = 0;
static uintptr_t thread3_id = 0;


/* Thread 1 calls oio_async_send on async_handle_1 20 times. */
void thread1_entry(void *arg) {
  int i;

  for (i = 0; i < 20; i++) {
    oio_async_send(&async1_handle);
    oio_sleep(50);
  }
}


/* Thread 2 calls oio_async_send on async_handle_2 8 times. */
void thread2_entry(void *arg) {
  int i;

  for (i = 0; i < 8; i++) {
    oio_async_send(&async2_handle);
    oio_sleep(50);
  }
}


/* Thread 3 calls oio_async_send on async_handle_2 8 times
 * after waiting half a second first.
 */
void thread3_entry(void *arg) {
  int i;

  oio_sleep(500);

  for (i = 0; i < 8; i++) {
    oio_async_send(&async2_handle);
    oio_sleep(50);
  }
}


static void close_cb(oio_handle_t* handle, int status) {
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  close_cb_called++;
}


static oio_buf alloc_cb(oio_handle_t* handle, size_t size) {
  oio_buf buf = {0, 0};
  FATAL("alloc should not be called");
  return buf;
}


static void async1_cb(oio_handle_t* handle, int status) {
  ASSERT(handle == &async1_handle);
  ASSERT(status == 0);

  async1_cb_called++;
  printf("async1_cb #%d\n", async1_cb_called);

  if (async1_cb_called == 20) {
    oio_close(handle);
  }
}


static void async2_cb(oio_handle_t* handle, int status) {
  ASSERT(handle == &async2_handle);
  ASSERT(status == 0);

  async2_cb_called++;
  printf("async2_cb #%d\n", async2_cb_called);

  if (async2_cb_called == 16) {
    oio_close(handle);
  }
}


static void prepare_cb(oio_handle_t* handle, int status) {
  int r;

  ASSERT(handle == &prepare_handle);
  ASSERT(status == 0);

  switch (prepare_cb_called) {
    case 0:
      thread1_id = oio_create_thread(thread1_entry, NULL);
      ASSERT(thread1_id != 0);
      break;

    case 1:
      thread2_id = oio_create_thread(thread2_entry, NULL);
      ASSERT(thread2_id != 0);
      break;

    case 2:
      thread3_id = oio_create_thread(thread3_entry, NULL);
      ASSERT(thread3_id != 0);
      break;

    case 3:
      r = oio_close(handle);
      ASSERT(r == 0);
      break;

    case 4:
      FATAL("Should never get here");
  }

  prepare_cb_called++;
}


TEST_IMPL(async) {
  int r;

  oio_init(alloc_cb);

  r = oio_prepare_init(&prepare_handle, close_cb, NULL);
  ASSERT(r == 0);
  r = oio_prepare_start(&prepare_handle, prepare_cb);
  ASSERT(r == 0);

  r = oio_async_init(&async1_handle, async1_cb, close_cb, NULL);
  ASSERT(r == 0);
  r = oio_async_init(&async2_handle, async2_cb, close_cb, NULL);
  ASSERT(r == 0);

  r = oio_run();
  ASSERT(r == 0);

  r = oio_wait_thread(thread1_id);
  ASSERT(r == 0);
  r = oio_wait_thread(thread2_id);
  ASSERT(r == 0);
  r = oio_wait_thread(thread3_id);
  ASSERT(r == 0);

  ASSERT(prepare_cb_called == 4);
  ASSERT(async1_cb_called = 20);
  ASSERT(async2_cb_called = 16);
  ASSERT(close_cb_called == 3);

  return 0;
}
