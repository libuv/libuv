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

#include "task.h"
#include "../oio.h"

#include <math.h>
#include <stdio.h>


#define TARGET_CONNECTIONS          100
#define WRITE_BUFFER_SIZE           8192
#define MAX_SIMULTANEOUS_CONNECTS   100

#define STATS_INTERVAL              1000 /* msec */
#define RUN_TIME                    5000 /* msec */


static void do_write(oio_handle_t* handle);
static void maybe_connect_some();

static oio_req_t* req_alloc();
static void req_free(oio_req_t* oio_req);

static oio_buf buf_alloc(oio_handle_t* handle, size_t size);
static void buf_free(oio_buf oio_buf);


static struct sockaddr_in server_addr;

static int max_connect_socket = 0;
static int read_sockets = 0;
static int write_sockets = 0;

static int64_t read_total = 0;
static int64_t written_total = 0;

static int stats_left = 0;

static char write_buffer[WRITE_BUFFER_SIZE];

static oio_handle_t read_handles[TARGET_CONNECTIONS];
static oio_handle_t write_handles[TARGET_CONNECTIONS];


static int mbit(int64_t bytes, int64_t passed_milis) {
  return (int)(bytes / (125 * passed_milis));
}


static void show_stats(oio_req_t *req, int64_t skew, int status) {
  int64_t msec = STATS_INTERVAL + skew;

  LOGF("connections: %d, read: %d mbit/s, write: %d mbit/s\n",
       read_sockets,
       mbit(read_total, msec),
       mbit(written_total, msec));

  /* Exit if the show is over */
  if (!--stats_left) {
    exit(0);
  }

  /* Reset read and write counters */
  read_total = 0;
  written_total = 0;

  oio_timeout(req, (STATS_INTERVAL - skew > 0)
                   ? STATS_INTERVAL - skew
                   : 0);
}


static void start_stats_collection() {
  oio_req_t* req = req_alloc();
  int r;

  /* Show-stats timeout */
  stats_left = (int)ceil((double)RUN_TIME / (double)STATS_INTERVAL);
  oio_req_init(req, NULL, (void*)show_stats);
  r = oio_timeout(req, STATS_INTERVAL);
  ASSERT(r == 0);
}


void close_cb(oio_handle_t* handle, int status) {
  ASSERT(status == 0);
}


static void read_cb(oio_handle_t* handle, int bytes, oio_buf buf) {
  ASSERT(bytes >= 0);

  buf_free(buf);

  read_total += bytes;
}


static void write_cb(oio_req_t *req, int status) {
  oio_buf* buf = (oio_buf*) req->data;

  ASSERT(status == 0);

  req_free(req);

  written_total += sizeof write_buffer;
  do_write(req->handle);
}


static void do_write(oio_handle_t* handle) {
  oio_req_t* req;
  oio_buf buf;
  int r;

  buf.base = (char*) &write_buffer;
  buf.len = sizeof write_buffer;

  while (handle->write_queue_size == 0) {
    req = req_alloc();
    oio_req_init(req, handle, write_cb);

    r = oio_write(req, &buf, 1);
    ASSERT(r == 0);
  }
}

static void maybe_start_writing() {
  int i;

  if (read_sockets == TARGET_CONNECTIONS &&
      write_sockets == TARGET_CONNECTIONS) {
    start_stats_collection();

    /* Yay! start writing */
    for (i = 0; i < write_sockets; i++) {
      do_write(&write_handles[i]);
    }
  }
}


static void connect_cb(oio_req_t* req, int status) {
  if (status) LOG(oio_strerror(oio_last_error()));
  ASSERT(status == 0);

  write_sockets++;
  req_free(req);

  maybe_connect_some();
  maybe_start_writing();
}


static void do_connect(oio_handle_t* handle, struct sockaddr* addr) {
  oio_req_t* req;
  int r;

  r = oio_tcp_init(handle, close_cb, NULL);
  ASSERT(r == 0);

  req = req_alloc();
  oio_req_init(req, handle, connect_cb);
  r = oio_connect(req, addr);
  ASSERT(r == 0);
}


static void maybe_connect_some() {
  while (max_connect_socket < TARGET_CONNECTIONS &&
         max_connect_socket < write_sockets + MAX_SIMULTANEOUS_CONNECTS) {
    do_connect(&write_handles[max_connect_socket++],
               (struct sockaddr*) &server_addr);
  }
}


static void accept_cb(oio_handle_t* server) {
  oio_handle_t* handle;
  int r;

  ASSERT(read_sockets < TARGET_CONNECTIONS);
  handle = &read_handles[read_sockets];

  r = oio_accept(server, handle, close_cb, NULL);
  ASSERT(r == 0);

  r = oio_read_start(handle, read_cb);
  ASSERT(r == 0);

  read_sockets++;

  maybe_start_writing();
}


BENCHMARK_IMPL(pump) {
  oio_handle_t server;
  int r;

  oio_init(buf_alloc);

  /* Server */
  server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  r = oio_tcp_init(&server, close_cb, NULL);
  ASSERT(r == 0);
  r = oio_bind(&server, (struct sockaddr*) &server_addr);
  ASSERT(r == 0);
  r = oio_listen(&server, TARGET_CONNECTIONS, accept_cb);
  ASSERT(r == 0);

  /* Start making connections */
  maybe_connect_some();

  oio_run();

  return 0;
}


/*
 * Request allocator
 */

typedef struct req_list_s {
  oio_req_t oio_req;
  struct req_list_s* next;
} req_list_t;


static req_list_t* req_freelist = NULL;


static oio_req_t* req_alloc() {
  req_list_t* req;

  req = req_freelist;
  if (req != NULL) {
    req_freelist = req->next;
    return (oio_req_t*) req;
  }

  req = (req_list_t*) malloc(sizeof *req);
  return (oio_req_t*) req;
}


static void req_free(oio_req_t* oio_req) {
  req_list_t* req = (req_list_t*) oio_req;

  req->next = req_freelist;
  req_freelist = req;
}


/*
 * Buffer allocator
 */

typedef struct buf_list_s {
  oio_buf oio_buf;
  struct buf_list_s* next;
} buf_list_t;


static buf_list_t* buf_freelist = NULL;


static oio_buf buf_alloc(oio_handle_t* handle, size_t size) {
  buf_list_t* buf;

  buf = buf_freelist;
  if (buf != NULL) {
    buf_freelist = buf->next;
    return buf->oio_buf;
  }

  buf = (buf_list_t*) malloc(size + sizeof *buf);
  buf->oio_buf.len = (unsigned int)size;
  buf->oio_buf.base = ((char*) buf) + sizeof *buf;

  return buf->oio_buf;
}


static void buf_free(oio_buf oio_buf) {
  buf_list_t* buf = (buf_list_t*) (oio_buf.base - sizeof *buf);

  buf->next = buf_freelist;
  buf_freelist = buf;
}
