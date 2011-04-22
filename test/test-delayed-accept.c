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


static char BUFFER[1024];

static int accept_cb_called = 0;
static int do_accept_called = 0;
static int close_cb_called = 0;
static int connect_cb_called = 0;


static void close_cb(oio_handle* handle, int status) {
  ASSERT(handle != NULL);
  ASSERT(status == 0);

  free(handle);

  close_cb_called++;
}


static void do_accept(oio_req* req, int status) {
  oio_handle* server;
  oio_handle* accepted_handle = (oio_handle*)malloc(sizeof *accepted_handle);
  int r;

  ASSERT(req != NULL);
  ASSERT(status == 0);
  ASSERT(accepted_handle != NULL);

  server = (oio_handle*)req->data;
  r = oio_accept(server, accepted_handle, close_cb, NULL);
  ASSERT(r == 0);

  do_accept_called++;

  /* Immediately close the accepted handle. */
  oio_close(accepted_handle);

  /* After accepting the two clients close the server handle */
  if (do_accept_called == 2) {
    oio_close(server);
  }

  free(req);
}


static void accept_cb(oio_handle* handle) {
  oio_req* timeout_req = (oio_req*)malloc(sizeof *timeout_req);

  ASSERT(timeout_req != NULL);

  /* Accept the client after 1 second */
  oio_req_init(timeout_req, NULL, &do_accept);
  timeout_req->data = (void*)handle;
  oio_timeout(timeout_req, 1000);

  accept_cb_called++;
}


static void start_server() {
  struct sockaddr_in addr = oio_ip4_addr("0.0.0.0", TEST_PORT);
  oio_handle* server = (oio_handle*)malloc(sizeof *server);
  int r;

  ASSERT(server != NULL);

  r = oio_tcp_init(server, close_cb, NULL);
  ASSERT(r == 0);

  r = oio_bind(server, (struct sockaddr*) &addr);
  ASSERT(r == 0);

  r = oio_listen(server, 128, accept_cb);
  ASSERT(r == 0);
}


static void read_cb(oio_req* req, size_t nread, int status) {
  /* The server will not send anything, it should close gracefully. */
  ASSERT(req != NULL);
  ASSERT(status == 0);
  ASSERT(nread == 0);

  oio_close(req->handle);

  free(req);
}


static void connect_cb(oio_req* req, int status) {
  oio_buf buf;
  int r;

  ASSERT(req != NULL);
  ASSERT(status == 0);

  /* Reuse the req to do a read. */
  /* Not that the server will send anything, but otherwise we'll never know */
  /* when te server closes the connection. */
  oio_req_init(req, req->handle, read_cb);
  buf.base = (char*)&BUFFER;
  buf.len = sizeof BUFFER;
  r = oio_read(req, &buf, 1);
  ASSERT(r == 0);

  connect_cb_called++;
}


static void client_connect() {
  struct sockaddr_in addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  oio_handle* client = (oio_handle*)malloc(sizeof *client);
  oio_req* connect_req = (oio_req*)malloc(sizeof *connect_req);
  int r;

  ASSERT(client != NULL);
  ASSERT(connect_req != NULL);

  r = oio_tcp_init(client, close_cb, NULL);
  ASSERT(r == 0);

  oio_req_init(connect_req, client, connect_cb);
  r = oio_connect(connect_req, (struct sockaddr*)&addr);
  ASSERT(r == 0);
}


TEST_IMPL(delayed_accept) {
  oio_init();

  start_server();

  client_connect();
  client_connect();

  oio_run();

  ASSERT(accept_cb_called == 2);
  ASSERT(do_accept_called == 2);
  ASSERT(connect_cb_called == 2);
  ASSERT(close_cb_called == 5);

  return 0;
}
