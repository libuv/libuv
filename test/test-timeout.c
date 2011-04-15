#include "../oio.h"
#include "test.h"


int expected = 0;
int timeouts = 0;


void timeout_cb(oio_req *req) {
  ASSERT(req != NULL);
  free(req);
  timeouts++;
}

void exit_timeout_cb(oio_req *req) {
  ASSERT(req != NULL);
  free(req);
  ASSERT(timeouts == expected);
  exit(0);
}

void dummy_timeout_cb(oio_req *req) {
  /* Should never be called */
  FATAL(dummy_timer_cb should never be called)
}


TEST_IMPL(timeout) {
  oio_req *req;
  oio_req exit_req;
  oio_req dummy_req;
  int i;

  oio_init();

  /* Let 10 timers time out it 500 ms. */
  for (i = 0; i < 10; i++) {
    req = (oio_req*)malloc(sizeof(*req));
    ASSERT(req != NULL)

    oio_req_init(req, NULL, timeout_cb);

    if (oio_timeout(req, i * 50) < 1)
      FATAL(oio_timeout failed)

    expected++;
  }

  /* The 11th timer exits the test and runs after 1 s. */
  oio_req_init(&exit_req, NULL, exit_timeout_cb);
  if (oio_timeout(&exit_req, 1000) < 0)
    FATAL(oio_timeout failed)

  /* The 12th timer should never run. */
  oio_req_init(&dummy_req, NULL, dummy_timeout_cb);
  if (oio_timeout(&dummy_req, 2000))
    FATAL(oio_timeout failed)

  oio_run();

  FATAL(should never get here)
  return 2;
}