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

static int completed_pingers = 0;

#if defined(__CYGWIN__) || defined(__MSYS__)
#define NUM_PINGS 100 /* fewer pings to avoid timeout */
#else
#define NUM_PINGS 1000
#endif

/* 64 bytes is enough for a pinger */
#define BUFSIZE 10240

static char PING[] = "PING\n";
static char PONG[] = "PONG\n";
static int pinger_on_connect_count;


typedef struct {
  int pongs;
  int state;
  union {
    uv_tcp_t tcp;
    uv_pipe_t pipe;
  } stream;
  uv_connect_t connect_req;
  char read_buffer[BUFSIZE];
  char *PONG;
} pinger_t;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}


static void pinger_on_close(uv_handle_t* handle) {
  pinger_t* pinger = (pinger_t*)handle->data;

  ASSERT(NUM_PINGS == pinger->pongs);

  if (handle == (uv_handle_t*) &pinger->stream.tcp)
    free(pinger);
  else
    free(handle);

  completed_pingers++;
}


static void ponger_on_close(uv_handle_t* handle) {
  if (handle->data)
    free(handle->data);
  else
    free(handle);
}


static void pinger_after_write(uv_write_t *req, int status) {
  ASSERT(status == 0);
  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  uv_write_t *req;
  uv_buf_t buf;

  buf = uv_buf_init(PING, sizeof(PING) - 1);

  req = malloc(sizeof(*req));
  if (uv_write(req,
               (uv_stream_t*) &pinger->stream.tcp,
               &buf,
               1,
               pinger_after_write)) {
    FATAL("uv_write failed");
  }

  puts("PING");
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

    uv_close((uv_handle_t*)stream, pinger_on_close);
    if (stream != (uv_stream_t*) &pinger->stream.tcp)
      uv_close((uv_handle_t*) &pinger->stream.tcp, ponger_on_close);

    return;
  }

  /* Now we count the pongs */
  for (i = 0; i < nread; i++) {
    ASSERT(buf->base[i] == pinger->PONG[pinger->state]);
    pinger->state = (pinger->state + 1) % strlen(pinger->PONG);

    if (pinger->state != 0)
      continue;

    printf("PONG %d\n", pinger->pongs);
    pinger->pongs++;

    if (pinger->pongs < NUM_PINGS) {
      pinger_write_ping(pinger);
    } else {
      uv_close((uv_handle_t*)stream, pinger_on_close);
      if (stream != (uv_stream_t*) &pinger->stream.tcp)
        uv_close((uv_handle_t*) &pinger->stream.tcp, ponger_on_close);
      break;
    }
  }

  free(buf->base);
}


static void ponger_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  uv_buf_t writebuf;
  uv_write_t *req;
  int i;

  if (nread < 0) {
    ASSERT(nread == UV_EOF);

    puts("got EOF");
    free(buf->base);

    uv_close((uv_handle_t*)stream, ponger_on_close);

    return;
  }

  /* Echo back */
  for (i = 0; i < nread; i++) {
    if (buf->base[i] == 'I')
      buf->base[i] = 'O';
  }

  writebuf = uv_buf_init(buf->base, nread);
  req = malloc(sizeof(*req));
  if (uv_write(req,
               stream,
               &writebuf,
               1,
               pinger_after_write)) {
    FATAL("uv_write failed");
  }
}


static void pinger_on_connect(uv_connect_t *req, int status) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  pinger_on_connect_count++;

  ASSERT(status == 0);

  ASSERT(1 == uv_is_readable(req->handle));
  ASSERT(1 == uv_is_writable(req->handle));
  ASSERT(0 == uv_is_closing((uv_handle_t *) req->handle));

  pinger_write_ping(pinger);

  uv_read_start((uv_stream_t*)(req->handle), alloc_cb, pinger_read_cb);
}


/* same ping-pong test, but using IPv6 connection */
static void tcp_pinger_v6_new(void) {
  int r;
  struct sockaddr_in6 server_addr;
  pinger_t *pinger;


  ASSERT(0 ==uv_ip6_addr("::1", TEST_PORT, &server_addr));
  pinger = malloc(sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->PONG = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT(!r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static void tcp_pinger_new(void) {
  int r;
  struct sockaddr_in server_addr;
  pinger_t *pinger;

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  pinger = malloc(sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->PONG = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT(!r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static void pipe_connect_pinger_new(void) {
  int r;
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->PONG = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_pipe_init(uv_default_loop(), &pinger->stream.pipe, 0);
  pinger->stream.pipe.data = pinger;
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */

  uv_pipe_connect(&pinger->connect_req, &pinger->stream.pipe, TEST_PIPENAME,
      pinger_on_connect);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT(pinger_on_connect_count == 0);
}


static void socketpair_pinger_new(void) {
  pinger_t *pinger;
  uv_os_sock_t fds[2];
  uv_tcp_t *ponger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->PONG = PONG;

  /* Try to make a socketpair and do NUM_PINGS ping-pongs. */
  (void)uv_default_loop(); /* ensure WSAStartup has been performed */
  ASSERT(0 == uv_socketpair(SOCK_STREAM, 0, fds));

  ASSERT(0 == uv_tcp_init(uv_default_loop(), &pinger->stream.tcp));
  pinger->stream.pipe.data = pinger;
  ASSERT(0 == uv_tcp_open(&pinger->stream.tcp, fds[1]));

  ponger = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  ASSERT(ponger != NULL);
  ponger->data = NULL;
  ASSERT(0 == uv_tcp_init(uv_default_loop(), ponger));
  ASSERT(0 == uv_tcp_open(ponger, fds[0]));

  pinger_write_ping(pinger);

  uv_read_start((uv_stream_t*)&pinger->stream.tcp, alloc_cb, pinger_read_cb);
  uv_read_start((uv_stream_t*)ponger, alloc_cb, ponger_read_cb);
}


static void pipe_pinger_new(void) {
  uv_os_fd_t fds[2];
  pinger_t *pinger;
  uv_pipe_t *ponger;

  /* Try to make a pipe and do NUM_PINGS pings. */
  ASSERT(0 == uv_pipe(fds, 1, 1));

  ponger = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
  ASSERT(ponger != NULL);
  ASSERT(0 == uv_pipe_init(uv_default_loop(), ponger, 0));
  ASSERT(0 == uv_pipe_open(ponger, fds[0]));

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  ASSERT(pinger != NULL);
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->PONG = PING;
  ASSERT(0 == uv_pipe_init(uv_default_loop(), &pinger->stream.pipe, 0));
  ASSERT(0 == uv_pipe_open(&pinger->stream.pipe, fds[1]));
  pinger->stream.pipe.data = pinger; /* record for close_cb */
  ponger->data = pinger; /* record for read_cb */

  pinger_write_ping(pinger);

  uv_read_start((uv_stream_t*) ponger, alloc_cb, pinger_read_cb);
}


TEST_IMPL(tcp_ping_pong) {
  socketpair_pinger_new();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 1);

  tcp_pinger_new();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(tcp_ping_pong_v6) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  tcp_pinger_v6_new();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(pipe_ping_pong) {
  pipe_pinger_new();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 1);

  pipe_connect_pinger_new();
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(completed_pingers == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
