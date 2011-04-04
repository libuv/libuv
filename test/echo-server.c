#include "../ol.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>


#define BUFSIZE 1024


typedef struct {
  ol_handle* handle;
  ol_req req;
  ol_buf buf;
  char read_buffer[BUFSIZE];
} peer_t;


void after_write(ol_req* req);
void after_read(ol_req* req, size_t nread);
void try_read(peer_t* peer);
void on_close(ol_handle* peer, ol_err err);
void on_accept(ol_handle* server, ol_handle* new_client);


ol_handle *server = NULL;


void after_write(ol_req* req) {
  peer_t *peer = (peer_t*) req->data;
  try_read(peer);
}


void after_read(ol_req* req, size_t nread) {
  if (nread == 0) {
    ol_close(req->handle);
  } else {
    peer_t *peer = (peer_t*) req->data;
    peer->buf.len = nread;
    peer->req.cb = after_write;
    ol_write(peer->handle, &peer->req, &peer->buf, 1);
  }
}


void try_read(peer_t* peer) {
  peer->buf.len = BUFSIZE;
  peer->req.cb = after_read;
  ol_read(peer->handle, &peer->req, &peer->buf, 1);
}


void on_close(ol_handle* peer, ol_err err) {
  if (err) {
    fprintf(stdout, "Socket error\n");
  }

  ol_free(peer);
}


void on_accept(ol_handle* server, ol_handle* new_client) {
  peer_t* p;

  new_client->close_cb = on_close;

  p = (peer_t*)malloc(sizeof(peer_t));
  p->handle = new_client;
  p->buf.base = p->read_buffer;
  p->buf.len = BUFSIZE;
  p->req.data = p;
  ol_req_init(&p->req, NULL);

  try_read(p);
}


void on_server_close(ol_handle* handle, ol_err err) {
  assert(handle == server);

  if (err) {
    fprintf(stdout, "Socket error\n");
  }

  ol_free(server);
  server = NULL;
}


int echo_start(int port) {
  struct sockaddr_in addr = ol_ip4_addr("0.0.0.0", port);
  int r;

  assert(server == NULL);
  server = ol_tcp_handle_new(&on_server_close, NULL);

  r = ol_bind(server, (struct sockaddr*) &addr);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Bind error\n");
    return 1;
  }

  r = ol_listen(server, 128, on_accept);
  if (r) {
    /* TODO: Error codes */
    fprintf(stderr, "Listen error\n");
    return 1;
  }

  return 0;
}


int echo_stop() {
  assert(server != NULL);
  return ol_close(server);
}


TEST_IMPL(echo_server) {
  ol_init();
  if (echo_start(TEST_PORT))
    return 1;

  fprintf(stderr, "Listening!\n");
  ol_run();
  return 0;
}