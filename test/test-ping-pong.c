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

static int completed_pingers = 0;

#define NUM_PINGS 1000

/* 64 bytes is enough for a pinger */
#define BUFSIZE 10240

static char PING[] = "PING\n";


typedef struct {
  int pongs;
  int state;
  oio_handle handle;
  oio_req connect_req;
  oio_req read_req;
  oio_buf buf;
  char read_buffer[BUFSIZE];
} pinger_t;

void pinger_try_read(pinger_t* pinger);


void pinger_on_close(oio_handle* handle, oio_err err) {
  pinger_t* pinger = (pinger_t*)handle->data;

  ASSERT(!err);
  ASSERT(NUM_PINGS == pinger->pongs);

  free(pinger);

  completed_pingers++;
}


void pinger_after_write(oio_req *req) {
  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  oio_req *req;

  req = (oio_req*)malloc(sizeof(*req));
  oio_req_init(req, &pinger->handle, pinger_after_write);

  if (oio_write2(req, (char*)&PING)) {
    FATAL("oio_write2 failed");
  }

  puts("PING");
}


static void pinger_after_read(oio_req* req, size_t nread) {
  unsigned int i;
  pinger_t* pinger;

  pinger = (pinger_t*)req->handle->data;

  if (nread == 0) {
    puts("got EOF");
    oio_close(&pinger->handle);
    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT(pinger->buf.base[i] == PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      printf("PONG %d\n", pinger->pongs);
      pinger->pongs++;
      if (pinger->pongs < NUM_PINGS) {
        pinger_write_ping(pinger);
      } else {
        oio_close(&pinger->handle);
        return;
      }
    }
  }

  pinger_try_read(pinger);
}


void pinger_try_read(pinger_t* pinger) {
  oio_req_init(&pinger->read_req, &pinger->handle, pinger_after_read);
  oio_read(&pinger->read_req, &pinger->buf, 1);
}


void pinger_on_connect(oio_req *req, oio_err err) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  ASSERT(!err);

  pinger_try_read(pinger);
  pinger_write_ping(pinger);
}


void pinger_new() {
  int r;
  struct sockaddr_in client_addr = oio_ip4_addr("0.0.0.0", 0);
  struct sockaddr_in server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->buf.len = BUFSIZE;
  pinger->buf.base = (char*)&pinger->read_buffer;

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  r = oio_tcp_init(&pinger->handle, pinger_on_close, (void*)pinger);
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  oio_req_init(&pinger->connect_req, &pinger->handle, pinger_on_connect);

  oio_bind(&pinger->handle, (struct sockaddr*)&client_addr);
  r = oio_connect(&pinger->connect_req, (struct sockaddr*)&server_addr);
  ASSERT(!r);
}


TEST_IMPL(ping_pong) {
  oio_init();

  pinger_new();
  oio_run();

  ASSERT(completed_pingers == 1);

  return 0;
}
