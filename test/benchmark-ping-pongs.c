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
#include <string.h> /* strlen */

/* Run the benchmark for this many ms */
#define TIME 1000


typedef struct {
  int pongs;
  int state;
  oio_handle_t handle;
  oio_req_t connect_req;
  oio_req_t shutdown_req;
} pinger_t;

typedef struct buf_s {
  oio_buf oio_buf;
  struct buf_s* next;
} buf_t;


static char PING[] = "PING\n";

static buf_t* buf_freelist = NULL;

static int completed_pingers = 0;
static int64_t start_time;


static oio_buf buf_alloc(oio_handle_t* handle, size_t size) {
  buf_t* ab;

  ab = buf_freelist;

  if (ab != NULL) {
    buf_freelist = ab->next;
    return ab->oio_buf;
  }

  ab = (buf_t*) malloc(size + sizeof *ab);
  ab->oio_buf.len = size;
  ab->oio_buf.base = ((char*) ab) + sizeof *ab;

  return ab->oio_buf;
}


static void buf_free(oio_buf oio_buf) {
  buf_t* ab = (buf_t*) (oio_buf.base - sizeof *ab);

  ab->next = buf_freelist;
  buf_freelist = ab;
}


static void pinger_close_cb(oio_handle_t* handle, int status) {
  pinger_t* pinger;

  ASSERT(status == 0);

  pinger = (pinger_t*)handle->data;
  printf("%d pings\n", pinger->pongs);

  free(pinger);

  completed_pingers++;
}


static void pinger_write_cb(oio_req_t *req, int status) {
  ASSERT(status == 0);

  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  oio_req_t *req;
  oio_buf buf;

  buf.base = (char*)&PING;
  buf.len = strlen(PING);

  req = (oio_req_t*)malloc(sizeof(*req));
  oio_req_init(req, &pinger->handle, pinger_write_cb);

  if (oio_write(req, &buf, 1)) {
    FATAL("oio_write failed");
  }
}


static void pinger_shutdown_cb(oio_handle_t* handle, int status) {
  ASSERT(status == 0);
}


static void pinger_read_cb(oio_handle_t* handle, int nread, oio_buf buf) {
  unsigned int i;
  pinger_t* pinger;

  pinger = (pinger_t*)handle->data;

  if (nread < 0) {
    ASSERT(oio_last_error().code == OIO_EOF);

    if (buf.base) {
      buf_free(buf);
    }

    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT(buf.base[i] == PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      pinger->pongs++;
      if (oio_now() - start_time > TIME) {
        oio_req_init(&pinger->shutdown_req, handle, pinger_shutdown_cb);
        oio_shutdown(&pinger->shutdown_req);
        break;
        return;
      } else {
        pinger_write_ping(pinger);
      }
    }
  }

  buf_free(buf);
}


static void pinger_connect_cb(oio_req_t *req, int status) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  ASSERT(status == 0);

  pinger_write_ping(pinger);

  if (oio_read_start(req->handle, pinger_read_cb)) {
    FATAL("oio_read_start failed");
  }
}


static void pinger_new() {
  int r;
  struct sockaddr_in client_addr = oio_ip4_addr("0.0.0.0", 0);
  struct sockaddr_in server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  r = oio_tcp_init(&pinger->handle, pinger_close_cb, (void*)pinger);
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  oio_req_init(&pinger->connect_req, &pinger->handle, pinger_connect_cb);

  oio_bind(&pinger->handle, (struct sockaddr*)&client_addr);
  r = oio_connect(&pinger->connect_req, (struct sockaddr*)&server_addr);
  ASSERT(!r);
}


BENCHMARK_IMPL(ping_pongs) {
  oio_init(buf_alloc);
  start_time = oio_now();

  pinger_new();
  oio_run();

  ASSERT(completed_pingers == 1);

  return 0;
}
