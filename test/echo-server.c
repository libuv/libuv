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
  int msg;
} peer_t;

oio_handle server;

void after_write(oio_req* req, int status);
void after_read(oio_handle* handle, int nread, oio_buf buf);
void on_close(oio_handle* peer, int status);
void on_accept(oio_handle* handle);


void after_write(oio_req* req, int status) {
  peer_t* peer;

  ASSERT(status == 0);

  peer = (peer_t*) req->data;

  /* Free the read/write buffer */
  free(peer->buf.base);

  /* Start reading again */
  oio_read_start(req->handle, after_read);
}


void after_read(oio_handle* handle, int nread, oio_buf buf) {
  peer_t* peer;
  
  if (nread < 0) {
    /* Error or EOF */
    ASSERT (oio_last_error().code == OIO_EOF);
    
    if (buf.base) {
      free(buf.base);
    }

    oio_close(handle);
    return;
  } 

  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf.base);
    return;
  }

  oio_req_init(&peer->req, &peer->handle, after_write);
  peer->req.data = peer;
  peer->buf.base = buf.base;
  peer->buf.len = nread;
  if (oio_write(&peer->req, &peer->buf, 1)) {
    FATAL("oio_write failed");
  }
  oio_read_stop(handle);
}


void on_close(oio_handle* peer, int status) {
  if (status != 0) {
    fprintf(stdout, "Socket error\n");
  }
}


void on_accept(oio_handle* server) {
  peer_t* p = (peer_t*)calloc(sizeof(peer_t), 1);

  if (oio_accept(server, &p->handle, on_close, (void*)p)) {
    FATAL("oio_accept failed");
  }

  oio_read_start(server, after_read);
}


void on_server_close(oio_handle* handle, int status) {
  ASSERT(handle == &server);
  ASSERT(status == 0);
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


oio_buf echo_alloc(oio_handle* handle, size_t suggested_size) {
  oio_buf buf;
  buf.base = (char*) malloc(suggested_size);
  buf.len = suggested_size;
  return buf;
}


HELPER_IMPL(echo_server) {
  oio_init(echo_alloc);
  if (echo_start(TEST_PORT))
    return 1;

  fprintf(stderr, "Listening!\n");
  oio_run();
  return 0;
}
