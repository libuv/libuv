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

#include <stdlib.h>
#include <stdio.h>


static oio_handle handle;
static oio_req req;
static int connect_cb_calls;
static int close_cb_calls;


static void on_close(oio_handle* handle, int status) {
  ASSERT(status == 0);
  close_cb_calls++;
}


static void on_connect(oio_req *req, int status) {
  ASSERT(status == -1);
  ASSERT(oio_last_error().code == OIO_ECONNREFUSED);
  connect_cb_calls++;
  oio_close(req->handle);
}


TEST_IMPL(connection_fail) {
  struct sockaddr_in client_addr, server_addr;
  int r;

  oio_init();

  client_addr = oio_ip4_addr("0.0.0.0", 0);

  /* There should be no servers listening on this port. */
  server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  r = oio_tcp_init(&handle, on_close, NULL);
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  oio_req_init(&req, &handle, on_connect);

  oio_bind(&handle, (struct sockaddr*)&client_addr);
  r = oio_connect(&req, (struct sockaddr*)&server_addr);
  ASSERT(!r);

  oio_run();

  ASSERT(connect_cb_calls == 1);
  ASSERT(close_cb_calls == 1);

  return 0;
}
