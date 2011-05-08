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
  char read_buffer[BUFSIZE];
} pinger_t;

void pinger_try_read(pinger_t* pinger);


static void pinger_on_close(oio_handle* handle, int status) {
  pinger_t* pinger = (pinger_t*)handle->data;

  ASSERT(status == 0);
  ASSERT(NUM_PINGS == pinger->pongs);

  free(pinger);

  completed_pingers++;
}


static void pinger_after_write(oio_req *req, int status) {
  ASSERT(status == 0);

  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  oio_req *req;
  oio_buf buf;

  buf.base = (char*)&PING;
  buf.len = strlen(PING);

  req = (oio_req*)malloc(sizeof(*req));
  oio_req_init(req, &pinger->handle, pinger_after_write);

  if (oio_write(req, &buf, 1)) {
    FATAL("oio_write failed");
  }

  puts("PING");
}


static void pinger_read_cb(oio_handle* handle, int nread, oio_buf buf) {
  unsigned int i;
  pinger_t* pinger;

  pinger = (pinger_t*)handle->data;

  if (nread < 0) {
    ASSERT(oio_last_error().code == OIO_EOF);

    puts("got EOF");

    if (buf.base) {
      free(buf.base);
    }

    oio_close(&pinger->handle);

    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT(buf.base[i] == PING[pinger->state]);
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
}


static void pinger_on_connect(oio_req *req, int status) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  ASSERT(status == 0);

  pinger_write_ping(pinger);

  oio_read_start(req->handle, pinger_read_cb);
}


static void pinger_new() {
  int r;
  struct sockaddr_in server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  r = oio_tcp_init(&pinger->handle, pinger_on_close, (void*)pinger);
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  oio_req_init(&pinger->connect_req, &pinger->handle, pinger_on_connect);

  r = oio_connect(&pinger->connect_req, (struct sockaddr*)&server_addr);
  ASSERT(!r);
}


static oio_buf alloc_cb(oio_handle* handle, size_t size) {
  oio_buf buf;
  buf.base = (char*)malloc(size);
  buf.len = size;
  return buf;
}


TEST_IMPL(ping_pong) {
  oio_init(alloc_cb);

  pinger_new();
  oio_run();

  ASSERT(completed_pingers == 1);

  return 0;
}
