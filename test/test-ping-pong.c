#include "../ol.h"
#include "test.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static int completed_pingers = 0;

#define NUM_PINGS 1000

/* 64 bytes is enough for a pinger */
#define BUFSIZE 10240

static char PING[] = "PING\n";


typedef struct {
  int pongs;
  int state;
  ol_handle handle;
  ol_req connect_req;
  ol_req read_req;
  ol_buf buf;
  char read_buffer[BUFSIZE];
} pinger_t;

void pinger_try_read(pinger_t* pinger);


void pinger_on_close(ol_handle* handle, ol_err err) {
  pinger_t* pinger = (pinger_t*)handle->data;

  assert(!err);
  assert(NUM_PINGS == pinger->pongs);

  free(pinger);

  completed_pingers++;
}


void pinger_after_write(ol_req *req) {
  free(req);
}


void pinger_write_ping(pinger_t* pinger) {
  ol_req *req;
  int r;

  req = (ol_req*)malloc(sizeof(*req));
  ol_req_init(req, &pinger->handle, pinger_after_write);
  r = ol_write2(req, (char*)&PING);
  assert(!r);
}

void pinger_after_read(ol_req* req, size_t nread) {
  unsigned int i;
  pinger_t* pinger;

  pinger = (pinger_t*)req->handle->data;

  if (nread == 0) {
    ol_close(&pinger->handle);
    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    assert(pinger->buf.base[i] == PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      pinger->pongs++;
      if (pinger->pongs < NUM_PINGS) {
        pinger_write_ping(pinger);
      } else {
        ol_close(&pinger->handle);
        return;
      }
    }
  }

  pinger_try_read(pinger);
}


void pinger_try_read(pinger_t* pinger) {
  ol_read(&pinger->read_req, &pinger->buf, 1);
}


void pinger_on_connect(ol_req *req, ol_err err) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  if (err) {
    assert(0);
  }

  pinger_try_read(pinger);
  pinger_write_ping(pinger);
}


int pinger_new(int port) {
  struct sockaddr_in client_addr = ol_ip4_addr("0.0.0.0", 0);
  struct sockaddr_in server_addr = ol_ip4_addr("145.94.50.9", TEST_PORT);
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->buf.len = sizeof(pinger->read_buffer);
  pinger->buf.base = (char*)&pinger->read_buffer;

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  if (ol_tcp_handle_init(&pinger->handle, pinger_on_close, (void*)pinger)) {
    return -1;
  }

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  ol_req_init(&pinger->connect_req, &pinger->handle, pinger_on_connect);
  ol_req_init(&pinger->read_req, &pinger->handle, pinger_after_read);

  ol_bind(&pinger->handle, (struct sockaddr*)&client_addr);
  return ol_connect(&pinger->connect_req, (struct sockaddr*)&server_addr);
}


TEST_IMPL(ping_pong) {
  ol_init();

  if (pinger_new(8000)) {
    return 2;
  }

  ol_run();

  assert(completed_pingers == 1);

  return 0;
}
