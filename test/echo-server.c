#include "../ol.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 1024

typedef struct {
  ol_handle handle;
  ol_req req;
  ol_buf buf;
  char read_buffer[BUFSIZE];
} peer_t;

ol_handle server;

void after_write(ol_req* req);
void after_read(ol_req* req, size_t nread);
void try_read(peer_t* peer);
void on_close(ol_handle* peer, ol_err err);
void on_accept(ol_handle* handle);


void after_write(ol_req* req) {
  peer_t* peer = (peer_t*) req->data;
  try_read(peer);
}


void after_read(ol_req* req, size_t nread) {
  peer_t* peer;
  int r;

  if (nread == 0) {
    ol_close(req->handle);
  } else {
    peer = (peer_t*) req->data;
    peer->buf.len = nread;
    ol_req_init(&peer->req, &peer->handle, after_write);
    peer->req.data = peer;
    r = ol_write(&peer->req, &peer->buf, 1);
    assert(!r);
  }
}


void try_read(peer_t* peer) {
  int r;

  peer->buf.len = BUFSIZE;
  ol_req_init(&peer->req, &peer->handle, after_read);
  peer->req.data = peer;
  r = ol_read(&peer->req, &peer->buf, 1);
  assert(!r);
}


void on_close(ol_handle* peer, ol_err err) {
  if (err) {
    fprintf(stdout, "Socket error\n");
  }
}


void on_accept(ol_handle* server) {
  peer_t* p = (peer_t*)malloc(sizeof(peer_t));
  int r;

  r = ol_tcp_handle_accept(server, &p->handle, on_close, (void*)p);
  assert(!r);

  p->buf.base = (char*)&p->read_buffer;

  try_read(p);
}


void on_server_close(ol_handle* handle, ol_err err) {
  assert(handle == &server);

  if (err) {
    fprintf(stdout, "Socket error\n");
  }
}


int echo_start(int port) {
  struct sockaddr_in addr = ol_ip4_addr("0.0.0.0", port);
  int r;

  r = ol_tcp_handle_init(&server, on_server_close, NULL);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Socket creation error\n");
    return 1;
  }

  r = ol_bind(&server, (struct sockaddr*) &addr);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Bind error\n");
    return 1;
  }

  r = ol_listen(&server, 128, on_accept);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Listen error\n");
    return 1;
  }

  return 0;
}


int echo_stop() {
  return ol_close(&server);
}


TEST_IMPL(echo_server) {
  ol_init();
  if (echo_start(TEST_PORT))
    return 1;

  fprintf(stderr, "Listening!\n");
  ol_run();
  return 0;
}