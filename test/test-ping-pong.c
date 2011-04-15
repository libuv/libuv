#include "../oio.h"
#include "test.h"
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
  oio_handle handle;
  oio_req connect_req;
  oio_req read_req;
  oio_buf buf;
  char read_buffer[BUFSIZE];
} pinger_t;

void pinger_try_read(pinger_t* pinger);


void pinger_on_close(oio_handle* handle, oio_err err) {
  pinger_t* pinger = (pinger_t*)handle->data;

  ASSERT(!err)
  ASSERT(NUM_PINGS == pinger->pongs)

  free(pinger);

  completed_pingers++;
}


void pinger_after_write(oio_req *req) {
  free(req);
}


void pinger_write_ping(pinger_t* pinger) {
  oio_req *req;
  
  req = (oio_req*)malloc(sizeof(*req));
  oio_req_init(req, &pinger->handle, pinger_after_write);

  if (oio_write2(req, (char*)&PING))
    FATAL(oio_write2 failed)
}

void pinger_after_read(oio_req* req, size_t nread) {
  unsigned int i;
  pinger_t* pinger;

  pinger = (pinger_t*)req->handle->data;

  if (nread == 0) {
    oio_close(&pinger->handle);
    return;
  }

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT(pinger->buf.base[i] == PING[pinger->state])
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      pinger->pongs++;
      if (pinger->pongs < NUM_PINGS) {
        pinger_write_ping(pinger);
      } else {
        oio_close(&pinger->handle);
        return;
      }
    }
  }

  pinger_try_read(pinger);
}


void pinger_try_read(pinger_t* pinger) {
  oio_read(&pinger->read_req, &pinger->buf, 1);
}


void pinger_on_connect(oio_req *req, oio_err err) {
  pinger_t *pinger = (pinger_t*)req->handle->data;

  ASSERT(!err)

  pinger_try_read(pinger);
  pinger_write_ping(pinger);
}


int pinger_new() {
  struct sockaddr_in client_addr = oio_ip4_addr("0.0.0.0", 0);
  struct sockaddr_in server_addr = oio_ip4_addr("127.0.0.1", TEST_PORT);
  pinger_t *pinger;

  pinger = (pinger_t*)malloc(sizeof(*pinger));
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->buf.len = sizeof(pinger->read_buffer);
  pinger->buf.base = (char*)&pinger->read_buffer;

  /* Try to connec to the server and do NUM_PINGS ping-pongs. */
  if (oio_tcp_handle_init(&pinger->handle, pinger_on_close, (void*)pinger)) {
    return -1;
  }

  /* We are never doing multiple reads/connects at a time anyway. */
  /* so these handles can be pre-initialized. */
  oio_req_init(&pinger->connect_req, &pinger->handle, pinger_on_connect);
  oio_req_init(&pinger->read_req, &pinger->handle, pinger_after_read);

  oio_bind(&pinger->handle, (struct sockaddr*)&client_addr);
  return oio_connect(&pinger->connect_req, (struct sockaddr*)&server_addr);
}


TEST_IMPL(ping_pong) {
  oio_init();

  if (pinger_new()) {
    return 2;
  }

  oio_run();

  ASSERT(completed_pingers == 1)

  return 0;
}
