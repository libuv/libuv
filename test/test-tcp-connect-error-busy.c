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

#include "uv.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>


static void connect_cb(uv_connect_t* handle, int status) {
  FATAL("connect callback should not have been called!");
}


TEST_IMPL(tcp_connect_error_busy) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  int r;
  uv_connect_t req;
  uv_connect_t* connect_req = malloc(sizeof *connect_req);

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT(connect_req != NULL);

  r = uv_tcp_init(uv_default_loop(), &server);

  /* Pretend there's an active request, which should
   * result in -EBUSY when invoking uv_tcp_connect()
   */
  server.connect_req = connect_req;

  ASSERT(r == 0);
  r = uv_tcp_connect(&req,
                      &server,
                      (const struct sockaddr*) &addr,
                      connect_cb);
  ASSERT(r == -EBUSY);

  /* Remove mocked active request so the event loop
   * can shut down in MAKE_VALGRIND_HAPPY()
   */
  server.connect_req = NULL;

  MAKE_VALGRIND_HAPPY();
  return 0;
}
