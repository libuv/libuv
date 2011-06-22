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

#include "../uv.h"
#include "task.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h> /* strlen */



static uv_buf_t alloc_cb(uv_handle_t* handle, size_t size) {
  uv_buf_t buf;
  buf.base = (char*)malloc(size);
  buf.len = size;
  return buf;
}

int getaddrinfo_callbacks;

uv_getaddrinfo_t getaddrinfo_handle;

static void getaddrinfo_callback(uv_getaddrinfo_t* handle, int status, struct addrinfo* res) {
  ASSERT(handle == &getaddrinfo_handle);
  getaddrinfo_callbacks++;
}


/* data used for running multiple calls concurrently */
#define CONCURRENT_COUNT    10
int callback_counts[CONCURRENT_COUNT];
uv_getaddrinfo_t getaddrinfo_handles[CONCURRENT_COUNT];


static void getaddrinfo_cuncurrent_cb(uv_getaddrinfo_t* handle, int status, struct addrinfo* res) {
  int i;
  for (i = 0; i < CONCURRENT_COUNT; i++) {
    if (&getaddrinfo_handles[i] == handle) {
      callback_counts[i]++;
      break;
    }
  }

  ASSERT (i < CONCURRENT_COUNT);
}


TEST_IMPL(getaddrinfo) {

  int rc = 0;
  char* name = "localhost";
  int i;

  uv_init();

  printf("Start basic getaddrinfo test\n");

  getaddrinfo_callbacks = 0;

  uv_getaddrinfo(&getaddrinfo_handle, 
                 &getaddrinfo_callback,
                 name,
                 NULL,
                 NULL);

  uv_run();

  ASSERT(getaddrinfo_callbacks == 1);

  printf("Done basic getaddrinfo test\n");


  printf("Start multiple getaddrinfo sequential test\n");

  getaddrinfo_callbacks = 0;

  uv_getaddrinfo(&getaddrinfo_handle, 
                 &getaddrinfo_callback,
                 name,
                 NULL,
                 NULL);

  uv_run();

  ASSERT(getaddrinfo_callbacks == 1);

  getaddrinfo_callbacks = 0;

  uv_getaddrinfo(&getaddrinfo_handle, 
                 &getaddrinfo_callback,
                 name,
                 NULL,
                 NULL);

  uv_run();

  ASSERT(getaddrinfo_callbacks == 1);

  
  printf("Done multiple getaddrinfo sequential test test\n");


  printf("Start multiple getaddrinfo concurrent test\n");

  for (i = 0; i < CONCURRENT_COUNT; i++)
  {
    callback_counts[i] = 0;

    uv_getaddrinfo(&getaddrinfo_handles[i], 
                   &getaddrinfo_cuncurrent_cb,
                   name,
                   NULL,
                   NULL);

  }

  uv_run();

  for (i = 0; i < CONCURRENT_COUNT; i++)
  {
    ASSERT(callback_counts[i] == 1);
  }

  printf("Done multiple getaddrinfo concurrent test\n");

  return 0;
}
