#include "../ol.h"
#include "echo.h"
#include <assert.h>
#include <stdlib.h>

static int completed_pingers = 0;
static ol_req connect_req;

/* 64 bytes is enough for a pinger */
#define BUFSIZE 64

static char* PING = "PING\n";

typedef struct {
  int pongs;
  int state;
  ol_handle* handle;
  ol_req req;
  ol_buf buf;
  char read_buffer[BUFSIZE];
} pinger;


void pinger_on_close(ol_handle* handle, ol_err err) {
  assert(!err);
  pinger* p = handle->data;
  assert(1000 == p->pongs);
  free(p);
  ol_free(handle);
  completed_pingers++;
}


void pinger_after_read(ol_req* req, size_t nread, ol_err err) {
  int i, r;

  if (!err) {
    return;
  }

  if (nread == 0) {
    ol_close(req->handle);
    return;
  }

  pinger *p = req->data;

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    assert(p->buf.base[i] == PING[p->state]);
    /* 5 = strlen(PING) */
    p->state = (p->state + 1) % 5;
    if (p->state == 0) {
      p->pongs++;
      if (p->pongs < 1000) {
        r = ol_write2(p->handle, PING);
        assert(!r);
      } else {
        ol_close(p->handle);
      }
    }
  }
}


void pinger_try_read(pinger* pinger) {
  pinger->buf.len = BUFSIZE;
  pinger->req.cb = pinger_after_read;
  ol_read(pinger->handle, &pinger->req, &pinger->buf, 1);
}


void pinger_on_connect(ol_handle* handle, ol_err err) {
  int r;

  pinger *p = calloc(sizeof(pinger), 1);
  p->handle = handle;
  p->buf.base = p->read_buffer;
  p->buf.len = BUFSIZE;
  p->req.data = p;

  handle->data = p;

  pinger_try_read(p);

  r = ol_write2(handle, PING);
  if (r < 0) {
    /* error */
    assert(0);
  }
}


int pinger_connect(int port) {
  /* Try to connec to the server and do 1000 ping-pongs. */
  ol_handle* handle = ol_tcp_handle_new(pinger_on_close, NULL);
  struct sockaddr_in addr = ol_ip4_addr("127.0.0.1", port);
  return ol_connect(handle, &connect_req, (struct sockaddr*)&addr);
}


int main(int argc, char** argv) {
  ol_init();

  if (echo_start(8000)) {
    return 1;
  }

  if (pinger_connect(8000)) {
    return 2;
  }

  ol_run();

  assert(completed_pingers == 1);

  return 0;
}
