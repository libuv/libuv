#include "../ol.h"
#include "test.h"


int nested = 0;
int close_cb_called = 0;


void close_cb(ol_handle *handle, ol_err e) {
  assert("ol_close error" && e == 0);
  assert("ol_close_cb not called from a fresh stack" && nested == 0);
  close_cb_called++;
}


TEST_IMPL(close_cb_stack) {
  ol_handle handle;
  int r;

  ol_init();

  r = ol_tcp_handle_init(&handle, &close_cb, NULL);
  assert(!r);

  nested++;
  r = ol_close(&handle);
  assert(!r);
  nested--;

  ol_run();

  assert("ol_close_cb not called exactly once" && close_cb_called);

  return 0;
}