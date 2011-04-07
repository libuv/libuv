#include "../oio.h"
#include "test.h"


int nested = 0;
int close_cb_called = 0;


void close_cb(oio_handle *handle, oio_err e) {
  assert("oio_close error" && e == 0);
  assert("oio_close_cb not called from a fresh stack" && nested == 0);
  close_cb_called++;
}


TEST_IMPL(close_cb_stack) {
  oio_handle handle;
  int r;

  oio_init();

  r = oio_tcp_handle_init(&handle, &close_cb, NULL);
  assert(!r);

  nested++;
  r = oio_close(&handle);
  assert(!r);
  nested--;

  oio_run();

  assert("oio_close_cb not called exactly once" && close_cb_called);

  return 0;
}