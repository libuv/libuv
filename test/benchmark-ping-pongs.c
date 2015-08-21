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

#include "uv.h"
#include "task.h"

#include <stdlib.h>
#include <stdio.h>

/* Run the benchmark for this many ms */
#define TIME 5000


typedef struct {
  int pongs;
  int state;
  uv_tcp_t tcp;
  uv_connect_t connect_req;
  uv_shutdown_t shutdown_req;
  uv_read_t read_req;
  char buf[32];
} pinger_t;


static char PING[] = "PING\n";

static uv_loop_t* loop;

static int pinger_shutdown_cb_called;
static int completed_pingers = 0;
static int64_t start_time;


static void pinger_close_cb(uv_handle_t* handle) {
  pinger_t* pinger;

  pinger = (pinger_t*)handle->data;
  fprintf(stderr, "ping_pongs: %d roundtrips/s\n", (1000 * pinger->pongs) / TIME);
  fflush(stderr);

  free(pinger);

  completed_pingers++;
}


static void pinger_write_cb(uv_write_t* req, int status) {
  ASSERT(status == 0);

  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  uv_write_t* req;
  uv_buf_t buf;

  buf = uv_buf_init(PING, sizeof(PING) - 1);

  req = malloc(sizeof *req);
  if (uv_write(req, (uv_stream_t*) &pinger->tcp, &buf, 1, pinger_write_cb)) {
    FATAL("uv_write failed");
  }
}


static void pinger_shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT(status == 0);
  pinger_shutdown_cb_called++;

  /*
   * The close callback has not been triggered yet. We must wait for EOF
   * until we close the connection.
   */
  ASSERT(completed_pingers == 0);
}


static void pinger_read_cb(uv_read_t* req, int status) {
  ssize_t i;
  pinger_t* pinger;
  int rearm_read = 1;

  pinger = req->handle->data;

  if (status < 0) {
    ASSERT(status == UV_EOF);
    ASSERT(pinger_shutdown_cb_called == 1);
    uv_close((uv_handle_t*)req->handle, pinger_close_cb);
    return;
  }

  /* Now we count the pings */
  for (i = 0; i < status; i++) {
    ASSERT(pinger->buf[i] == PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      pinger->pongs++;
      if (uv_now(loop) - start_time > TIME) {
        uv_shutdown(&pinger->shutdown_req,
                    req->handle,
                    pinger_shutdown_cb);
        rearm_read = 0;
        break;
      } else {
        pinger_write_ping(pinger);
      }
    }
  }

  if (rearm_read) {
    uv_buf_t buf = uv_buf_init(pinger->buf, sizeof(pinger->buf));
    ASSERT(0 == uv_read(&pinger->read_req, req->handle, &buf, 1, pinger_read_cb));
  }
}


static void pinger_connect_cb(uv_connect_t* req, int status) {
  pinger_t *pinger = (pinger_t*)req->handle->data;
  uv_buf_t buf;

  ASSERT(status == 0);

  pinger_write_ping(pinger);

  buf = uv_buf_init(pinger->buf, sizeof(pinger->buf));
  ASSERT(0 == uv_read(&pinger->read_req, req->handle, &buf, 1, pinger_read_cb));
}


static void pinger_new(void) {
  struct sockaddr_in client_addr;
  struct sockaddr_in server_addr;
  pinger_t *pinger;
  int r;

  ASSERT(0 == uv_ip4_addr("0.0.0.0", 0, &client_addr));
  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  pinger = malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(loop, &pinger->tcp);
  ASSERT(!r);

  pinger->tcp.data = pinger;

  ASSERT(0 == uv_tcp_bind(&pinger->tcp,
                          (const struct sockaddr*) &client_addr,
                          0));

  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_connect_cb);
  ASSERT(!r);
}


BENCHMARK_IMPL(ping_pongs) {
  loop = uv_default_loop();

  start_time = uv_now(loop);

  pinger_new();
  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
