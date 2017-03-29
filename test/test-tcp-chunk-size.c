#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uv.h"
#include "task.h"

static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int connect_cb_called;
static int close_cb_called;
static int connection_cb_called;
static uv_write_t write_req;

#define DATABUFFERSIZE 10 * 1024 * 1024

static char out[DATABUFFERSIZE];
static char in[DATABUFFERSIZE];
static int bytes_read;
static int largesocket;
static int large_socket_enabled;
static int maxread;

int tcp_chunk_size_test();
int uv_tcp_enable_largesocket(uv_tcp_t *tcp);

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void write_cb(uv_write_t* req, int status) {
  ASSERT(status == 0);
}

static void conn_alloc_cb(uv_handle_t* handle,
                          size_t size,
                          uv_buf_t* buf) {
  buf->base = in;
  buf->len = size;
}

static void conn_read_cb(uv_stream_t* stream,
                         ssize_t nread,
                         const uv_buf_t* buf) {
  if(large_socket_enabled == 0)
    ASSERT(nread <= STDTCPWINDOW);
  else
    maxread = maxread < nread ? nread : maxread;

  bytes_read += nread;

  if(bytes_read == DATABUFFERSIZE) {
    if(large_socket_enabled == 1) {
      /* Not every chunk will be enlarged. So a reasonable
       * thing to do is to track the chunk sizes and make
       * sure that at least the largest read is big enough. */
      ASSERT(maxread > STDTCPWINDOW);
      fprintf(stderr, "largest chunk got: %d\n", maxread);
    }

    uv_close((uv_handle_t*) &incoming, close_cb);
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
  }
}

static void connect_cb(uv_connect_t* req, int status) {
  int r;
  uv_buf_t buf;

  ASSERT(status == 0);
  connect_cb_called++;

  buf = uv_buf_init(out, sizeof(out));
  memset(out, 32, sizeof(out));
  r = uv_write(&write_req, req->handle, &buf, 1, write_cb);
  ASSERT(r == 0);
}


static void connection_cb(uv_stream_t* tcp, int status) {
  int r;
  ASSERT(status == 0);
  ASSERT(0 == uv_tcp_init(tcp->loop, &incoming));
  ASSERT(0 == uv_accept(tcp, (uv_stream_t*) &incoming));

  if(largesocket == 1) {
    r = uv_tcp_enable_largesocket((uv_tcp_t *) &incoming);
    ASSERT(r == 0 || r == UV_ENOSYS);
    if(r == 0)
      large_socket_enabled = 1;
  }
  ASSERT(0 == uv_read_start((uv_stream_t*) &incoming,
                            conn_alloc_cb,
                            conn_read_cb));

  connection_cb_called++;
}


static void start_server(void) {
  struct sockaddr_in addr;
  ASSERT(0 == uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT(0 == uv_tcp_init(uv_default_loop(), &server));
  ASSERT(0 == uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT(0 == uv_listen((uv_stream_t*) &server, 128, connection_cb));
}

/* Negative validation: without uv_enable_largesocket,
 * TCP read chunk sizes cannot exeed 64 KB.
 */
TEST_IMPL(tcp_chunk_size) {
  tcp_chunk_size_test();
  return 0;
}

/* With uv_enable_largesocket, validate that
 * at least one TCP read chunk exeeds 64 KB.
 */
TEST_IMPL(tcp_chunk_size_large) {
  largesocket = 1;
  tcp_chunk_size_test();
  return 0;
}

int tcp_chunk_size_test() {
  uv_connect_t connect_req;
  struct sockaddr_in addr;

  start_server();

  ASSERT(0 == uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT(0 == uv_tcp_init(uv_default_loop(), &client));
  ASSERT(0 == uv_tcp_connect(&connect_req,
                             &client,
                             (struct sockaddr*) &addr,
                             connect_cb));

  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT(connect_cb_called == 1);
  ASSERT(connection_cb_called == 1);
  ASSERT(close_cb_called == 3);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
