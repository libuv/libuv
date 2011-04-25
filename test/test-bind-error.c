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


static int close_cb_called = 0;


static void close_cb(oio_handle* handle, int status) {
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  close_cb_called++;
}


TEST_IMPL(bind_error_addrinuse) {
  struct sockaddr_in addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  oio_handle server1, server2;
  int r;

  oio_init();

  r = oio_tcp_init(&server1, close_cb, NULL);
  ASSERT(r == 0);

  r = oio_bind(&server1, (struct sockaddr*) &addr);
  ASSERT(r == 0);

  r = oio_tcp_init(&server2, close_cb, NULL);
  ASSERT(r == 0);

  r = oio_bind(&server2, (struct sockaddr*) &addr);
  ASSERT(r == -1);
  ASSERT(oio_last_error().code == OIO_EADDRINUSE);

  oio_close(&server1);
  oio_close(&server2);

  oio_run();

  ASSERT(close_cb_called == 2);

  return 0;
}
