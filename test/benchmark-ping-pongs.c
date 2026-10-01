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
  int state;
  int pongs;
  uv_tcp_t tcp;
  uv_connect_t connect_req;
  uv_shutdown_t shutdown_req;
  int64_t start_time;
  struct buf_s* buf_freelist;
  int shutdown_cb_called;
  int* completed_pingers;
} pinger_t;

typedef struct buf_s {
  uv_buf_t uv_buf_t;
  struct buf_s* next;
  pinger_t* pinger;
} buf_t;

static char PING[] = "PING\n";
static uv_loop_t* loop;

static void buf_alloc(uv_handle_t* tcp, size_t size, uv_buf_t* buf) {
  pinger_t* pinger;
  buf_t* ab;

  pinger = (pinger_t*)tcp->data;
  ab = pinger->buf_freelist;
  if (ab != NULL) {
    pinger->buf_freelist = ab->next;
    ab->next = NULL;
  } else {
    ab = malloc(sizeof(*ab) + size);
    ab->uv_buf_t.len = size;
    ab->uv_buf_t.base = (char*) (ab + 1);
    ab->next = NULL;
    ab->pinger = pinger;
  }

  *buf = ab->uv_buf_t;
}


static void buf_free(const uv_buf_t* buf) {
  buf_t* ab = (buf_t*) buf->base - 1;
  pinger_t* pinger = ab->pinger;
  ab->next = pinger->buf_freelist;
  pinger->buf_freelist = ab;
}


static void pinger_close_cb(uv_handle_t* handle) {
  pinger_t* pinger;
  buf_t* next;
  buf_t* ab;

  pinger = (pinger_t*)handle->data;
  (*pinger->completed_pingers)++;

  ab = pinger->buf_freelist;
  while (ab != NULL) {
    next = ab->next;
    free(ab);
    ab = next;
  }
}


static void pinger_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);

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
  pinger_t* pinger;
  ASSERT_OK(status);
  pinger = (pinger_t*)req->data;
  pinger->shutdown_cb_called++;
}


static void pinger_read_cb(uv_stream_t* tcp,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  int64_t now;
  ssize_t i;
  pinger_t* pinger;

  pinger = (pinger_t*)tcp->data;

  if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);

    if (buf->base) {
      buf_free(buf);
    }

    ASSERT_EQ(1, pinger->shutdown_cb_called);
    uv_close((uv_handle_t*)tcp, pinger_close_cb);

    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT_EQ(buf->base[i], PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      now = uv_now(loop);
      if (pinger->pongs == 0) {
        pinger->start_time = now;
      }
      pinger->pongs++;
      if (now - pinger->start_time > TIME) {
        pinger->shutdown_req.data = pinger;
        uv_shutdown(&pinger->shutdown_req,
                    (uv_stream_t*) tcp,
                    pinger_shutdown_cb);
        break;
      } else {
        pinger_write_ping(pinger);
      }
    }
  }

  buf_free(buf);
}


static void pinger_connect_cb(uv_connect_t* req, int status) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  ASSERT_OK(status);

  pinger_write_ping(pinger);

  if (uv_read_start(req->handle, buf_alloc, pinger_read_cb)) {
    FATAL("uv_read_start failed");
  }
}


static void pinger_init(pinger_t* pinger, int* completed_pingers) {
  struct sockaddr_in client_addr;
  struct sockaddr_in server_addr;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &client_addr));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->start_time = 0;
  pinger->buf_freelist = NULL;
  pinger->shutdown_cb_called = 0;
  pinger->completed_pingers = completed_pingers;

  r = uv_tcp_init(loop, &pinger->tcp);
  ASSERT(!r);

  pinger->tcp.data = pinger;

  ASSERT_OK(uv_tcp_bind(&pinger->tcp,
                        (const struct sockaddr*) &client_addr,
                        0));

  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_connect_cb);
  ASSERT(!r);
}


static void test_ping_pongs(int count) {
  int completed_pingers = 0;
  pinger_t* pingers;
  int i, sum;

  loop = uv_default_loop();

  pingers = malloc(count * sizeof(pinger_t));
  for (i=0; i<count; i++) {
    pinger_init(&pingers[i], &completed_pingers);
  }

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(completed_pingers, count);

  if (count == 1) {
    fprintf(stderr, "ping_pongs: %d roundtrips/s\n", (1000 * pingers[0].pongs) / TIME);
  } else {
    sum = 0;
    for (i=0; i<count; i++) {
      sum += pingers[i].pongs;
    }
    fprintf(stderr, "ping_pongs_%d: average %d roundtrips/s\n", count, (1000 * sum) / TIME / count);
  }
  fflush(stderr);

  free(pingers);
  MAKE_VALGRIND_HAPPY(loop);
}


BENCHMARK_IMPL(ping_pongs) {
  test_ping_pongs(1);
  return 0;
}


BENCHMARK_IMPL(ping_pongs_10) {
  test_ping_pongs(10);
  return 0;
}
