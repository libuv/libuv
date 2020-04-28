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

#include <stdio.h>
#include <stdlib.h>

static int completed_pingers = 0;

#if defined(__CYGWIN__) || defined(__MSYS__) || defined(__MVS__)
#define NUM_PINGS 100 /* fewer pings to avoid timeout */
#else
#define NUM_PINGS 1000
#endif

#ifndef _LP64
/* Something big, but small enough that malloc should succeed. */
#define BIG_WRITE_SIZE ((1u << 28u) + 1u) /* 256 MB */
#elif defined _WIN32
/* Emperically, NT can't handle a larger value. */
/* Instead, we see the kernel fails with 0xffffffff800705aa (Insufficient resources?), */
/* when passed a large value to WriteFile(Ex). */
#define BIG_WRITE_SIZE ((1u << 30u) + 1u)
#else
#define BIG_WRITE_SIZE ((1u << 31u) + 1u)
#endif

#define BIG_WRITEV_SIZE ((1u << 24u) + 1u)
#define BIG_WRITEV_NUM 256 /* 4 GB */

static char PING[] = "PING\n";
static int pinger_on_connect_count;


typedef struct {
  int vectored_writes;
  int big_writes;
  unsigned pongs;
  uint64_t state;
  union {
    uv_tcp_t tcp;
    uv_pipe_t pipe;
  } stream;
  uv_connect_t connect_req;
  char* big_ping_data;
} pinger_t;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  if (size < 1u << 20u)
    size = 1u << 20u; /* The libuv default is too low for optimal performance */
  if (size > INT32_MAX)
    /* ensure that alloc size doesn't overlap with error codes in read_cb */
    size = INT32_MAX;
  do {
    buf->base = malloc(size);
    buf->len = size;
  } while (buf->base == NULL && (size /= 2));
}


static void pinger_on_close(uv_handle_t* handle) {
  pinger_t* pinger = (pinger_t*)handle->data;

  if (!pinger->big_writes)
    ASSERT(NUM_PINGS == pinger->pongs);

  free(pinger->big_ping_data);
  free(pinger);

  completed_pingers++;
}


static void pinger_after_write(uv_write_t* req, int status) {
  ASSERT(status == 0);
  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  uv_write_t* req;
  uv_buf_t bufs[sizeof PING - 1];
  int i, nbufs;

  if (!pinger->vectored_writes) {
    /* Write a single buffer. */
    nbufs = 1;
    bufs[0] = uv_buf_init(PING, sizeof PING - 1);
  } else {
    /* Write multiple buffers, each with one byte in them. */
    nbufs = sizeof PING - 1;
    for (i = 0; i < nbufs; i++) {
      bufs[i] = uv_buf_init(&PING[i], 1);
    }
  }

  req = malloc(sizeof(*req));
  req->data = NULL;
  if (uv_write(req,
               (uv_stream_t*) &pinger->stream.tcp,
               bufs,
               nbufs,
               pinger_after_write)) {
    FATAL("uv_write failed");
  }

  puts("PING");
}

static void pinger_write_big_ping(pinger_t* pinger) {
  int i, nbufs;
  size_t nbytes;
  char* zeros;
  uv_write_t* req;
  uv_buf_t buf[BIG_WRITEV_NUM];

  if (pinger->vectored_writes) {
    nbytes = BIG_WRITEV_SIZE;
    nbufs = BIG_WRITEV_NUM;
  }
  else {
    nbytes = BIG_WRITE_SIZE;
    nbufs = 1;
  }
  zeros = calloc(1, nbytes);
  ASSERT(zeros);
  /* Fill a couple character. */
  zeros[0] = 'a';
  zeros[1] = 'b';
  zeros[2] = 'c';
  zeros[nbytes / 2] = 'x';
  zeros[nbytes - 2] = 'y';
  zeros[nbytes - 1] = 'z';
  pinger->big_ping_data = zeros;

  for (i = 0; i < nbufs; i++) {
    buf[i] = uv_buf_init(zeros, nbytes);
  }

  req = malloc(sizeof(*req));
  req->data = NULL;
  if (uv_write(req,
               (uv_stream_t*) &pinger->stream.tcp,
               buf,
               nbufs,
               pinger_after_write)) {
    FATAL("uv_write failed");
  }

  puts("BIG PING");
}



static void pinger_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  ssize_t i;
  pinger_t* pinger;

  pinger = (pinger_t*)stream->data;

  if (nread < 0) {
    ASSERT(nread == UV_EOF);

    puts("got EOF");
    free(buf->base);

    uv_close((uv_handle_t*) &pinger->stream.tcp, pinger_on_close);

    return;
  }

  if (pinger->big_writes) {
    uint64_t expect;
    /* Now we verify the pings */
    if (pinger->vectored_writes) {
      for (i = 0; i < nread; i++) {
        ASSERT(buf->base[i] == pinger->big_ping_data[pinger->state % BIG_WRITEV_SIZE]);
        pinger->state++;
      }
    }
    else {
      for (i = 0; i < nread; i++) {
        ASSERT(buf->base[i] == pinger->big_ping_data[pinger->state % BIG_WRITE_SIZE]);
        pinger->state++;
      }
    }

    if (pinger->vectored_writes)
      expect = BIG_WRITEV_NUM * (uint64_t) BIG_WRITEV_SIZE;
    else
      expect = BIG_WRITE_SIZE;
    ASSERT(pinger->state <= expect);
    if (pinger->state == expect)
      uv_close((uv_handle_t*)(&pinger->stream.tcp), pinger_on_close);
  } else {
    /* Now we count the pings */
    for (i = 0; i < nread; i++) {
      ASSERT(buf->base[i] == PING[pinger->state]);
      pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);

      if (pinger->state != 0)
        continue;

      printf("PONG %d\n", pinger->pongs);
      pinger->pongs++;

      if (pinger->pongs < NUM_PINGS) {
        pinger_write_ping(pinger);
      } else {
        uv_close((uv_handle_t*)(&pinger->stream.tcp), pinger_on_close);
        break;
      }
    }
  }

  free(buf->base);
}


static void pinger_on_connect(uv_connect_t* req, int status) {
  pinger_t* pinger = (pinger_t*)req->handle->data;

  pinger_on_connect_count++;

  ASSERT(status == 0);

  ASSERT(1 == uv_is_readable(req->handle));
  ASSERT(1 == uv_is_writable(req->handle));
  ASSERT(0 == uv_is_closing((uv_handle_t *) req->handle));

  if (pinger->big_writes) {
    pinger_write_big_ping(pinger);
  } else {
    pinger_write_ping(pinger);
  }
  uv_read_start((uv_stream_t*)(req->handle), alloc_cb, pinger_read_cb);
}


/* same ping-pong test, but using IPv6 connection */
static void tcp_pinger_v6_new(int vectored_writes, int big_writes) {
  int r;
  struct sockaddr_in6 server_addr;
  pinger_t* pinger;


  ASSERT(0 ==uv_ip6_addr("::1", TEST_PORT, &server_addr));
  pinger = calloc(1, sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->vectored_writes = vectored_writes;
  pinger->big_writes = big_writes;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT(!r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static void tcp_pinger_new(int vectored_writes, int big_writes) {
  int r;
  struct sockaddr_in server_addr;
  pinger_t* pinger;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  pinger = calloc(1, sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->vectored_writes = vectored_writes;
  pinger->big_writes = big_writes;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT(!r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static void pipe_pinger_new(int vectored_writes, int big_writes) {
  int r;
  pinger_t* pinger;

  pinger = calloc(1, sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->vectored_writes = vectored_writes;
  pinger->big_writes = big_writes;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_pipe_init(uv_default_loop(), &pinger->stream.pipe, 0);
  pinger->stream.pipe.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  uv_pipe_connect(&pinger->connect_req, &pinger->stream.pipe, TEST_PIPENAME,
      pinger_on_connect);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static int run_ping_pong_test(void) {
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(completed_pingers == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(tcp_ping_pong) {
  tcp_pinger_new(0, 0);
  return run_ping_pong_test();
}


TEST_IMPL(tcp_ping_pong_vec) {
  tcp_pinger_new(1, 0);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(0, 0);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong_vec) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(1, 0);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong) {
  pipe_pinger_new(0, 0);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong_vec) {
  pipe_pinger_new(1, 0);
  return run_ping_pong_test();
}

TEST_IMPL(tcp_ping_pong_big) {
  tcp_pinger_new(0, 1);
  return run_ping_pong_test();
}


TEST_IMPL(tcp_ping_pong_vec_big) {
#ifdef _WIN32
  RETURN_SKIP("Large writes not supported.");
#endif
  tcp_pinger_new(1, 1);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong_big) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(0, 1);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong_vec_big) {
#ifdef _WIN32
  RETURN_SKIP("Large writes not supported.");
#endif
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(1, 1);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong_big) {
  pipe_pinger_new(0, 1);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong_vec_big) {
#ifdef _WIN32
  RETURN_SKIP("Large writes not supported.");
#endif
  pipe_pinger_new(1, 1);
  return run_ping_pong_test();
}
