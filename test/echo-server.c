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

#define BUFSIZE 1024

typedef struct {
  oio_handle handle;
  oio_req req;
  oio_buf buf;
  char read_buffer[BUFSIZE];
  int msg;
} peer_t;

oio_handle server;

void after_write(oio_req* req);
void after_read(oio_req* req, size_t nread);
void try_read(peer_t* peer);
void on_close(oio_handle* peer, int err);
void on_accept(oio_handle* handle);


void after_write(oio_req* req) {
  peer_t* peer = (peer_t*) req->data;
  try_read(peer);
}


void after_read(oio_req* req, size_t nread) {
  peer_t* peer = req->data;

  if (nread == 0) {
    oio_close(req->handle);
  } else {
    peer->buf.len = nread;
    oio_req_init(&peer->req, &peer->handle, after_write);
    peer->req.data = peer;
    if (oio_write(&peer->req, &peer->buf, 1)) {
      FATAL("oio_write failed");
    }
  }
}


void try_read(peer_t* peer) {
  peer->buf.len = BUFSIZE;
  oio_req_init(&peer->req, &peer->handle, after_read);
  peer->req.data = peer;
  if (oio_read(&peer->req, &peer->buf, 1)) {
    FATAL("oio_read failed");
  }
}


void on_close(oio_handle* peer, int err) {
  if (err) {
    fprintf(stdout, "Socket error\n");
  }
}


void on_accept(oio_handle* server) {
  peer_t* p = (peer_t*)calloc(sizeof(peer_t), 1);

  if (oio_accept(server, &p->handle, on_close, (void*)p)) {
    FATAL("oio_accept failed");
  }

  p->buf.base = (char*)&p->read_buffer;

  try_read(p);
}


void on_server_close(oio_handle* handle, int err) {
  ASSERT(handle == &server);
  ASSERT(!err);
}


int echo_start(int port) {
  struct sockaddr_in addr = oio_ip4_addr("0.0.0.0", port);
  int r;

  r = oio_tcp_init(&server, on_server_close, NULL);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Socket creation error\n");
    return 1;
  }

  r = oio_bind(&server, (struct sockaddr*) &addr);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Bind error\n");
    return 1;
  }

  r = oio_listen(&server, 128, on_accept);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Listen error\n");
    return 1;
  }

  return 0;
}


int echo_stop() {
  return oio_close(&server);
}


HELPER_IMPL(echo_server) {
  oio_init();
  if (echo_start(TEST_PORT))
    return 1;

  fprintf(stderr, "Listening!\n");
  oio_run();
  return 0;
}
