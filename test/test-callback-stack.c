#include "../oio.h"
#include "test.h"


int nested = 0;
int close_cb_called = 0;


void close_cb(oio_handle *handle, oio_err err) {
  ASSERT(!err)
  ASSERT(nested == 0 && "oio_close_cb must be called from a fresh stack")
  close_cb_called++;
}


TEST_IMPL(close_cb_stack) {
  oio_handle handle;

  oio_init();

  if (oio_tcp_handle_init(&handle, &close_cb, NULL))
    FATAL(oio_tcp_handle_init failed)

  nested++;

  if (oio_close(&handle))
    FATAL(oio_close failed)

  nested--;

  oio_run();

  ASSERT(close_cb_called && "oio_close_cb must be called exactly once")

  return 0;
}