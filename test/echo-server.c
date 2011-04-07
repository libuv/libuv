#include "../oio.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 1024

typedef struct {
  oio_handle handle;
  oio_req req;
  oio_buf buf;
  char read_buffer[BUFSIZE];
} peer_t;

oio_handle server;

void after_write(oio_req* req);
void after_read(oio_req* req, size_t nread);
void try_read(peer_t* peer);
void on_close(oio_handle* peer, oio_err err);
void on_accept(oio_handle* handle);


void after_write(oio_req* req) {
  peer_t* peer = (peer_t*) req->data;
  try_read(peer);
}


void after_read(oio_req* req, size_t nread) {
  peer_t* peer;
  int r;

  if (nread == 0) {
    oio_close(req->handle);
  } else {
    peer = (peer_t*) req->data;
    peer->buf.len = nread;
    oio_req_init(&peer->req, &peer->handle, after_write);
    peer->req.data = peer;
    r = oio_write(&peer->req, &peer->buf, 1);
    assert(!r);
  }
}


void try_read(peer_t* peer) {
  int r;

  peer->buf.len = BUFSIZE;
  oio_req_init(&peer->req, &peer->handle, after_read);
  peer->req.data = peer;
  r = oio_read(&peer->req, &peer->buf, 1);
  assert(!r);
}


void on_close(oio_handle* peer, oio_err err) {
  if (err) {
    fprintf(stdout, "Socket error\n");
  }
}


void on_accept(oio_handle* server) {
  peer_t* p = (peer_t*)malloc(sizeof(peer_t));
  int r;

  r = oio_tcp_handle_accept(server, &p->handle, on_close, (void*)p);
  assert(!r);

  p->buf.base = (char*)&p->read_buffer;

  try_read(p);
}


void on_server_close(oio_handle* handle, oio_err err) {
  assert(handle == &server);

  if (err) {
    fprintf(stdout, "Socket error\n");
  }
}


int echo_start(int port) {
  struct sockaddr_in addr = oio_ip4_addr("0.0.0.0", port);
  int r;

  r = oio_tcp_handle_init(&server, on_server_close, NULL);
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


TEST_IMPL(echo_server) {
  oio_init();
  if (echo_start(TEST_PORT))
    return 1;

  fprintf(stderr, "Listening!\n");
  oio_run();
  return 0;
}