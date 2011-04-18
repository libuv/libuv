#include "../oio.h"
#include "test.h"


static int expected = 0;
static int timeouts = 0;

static int start_time;

static void timeout_cb(oio_req *req) {
  ASSERT(req != NULL);
  free(req);
  timeouts++;

  /* Just call this randomly for the code coverage. */
  oio_update_time();
}

static void exit_timeout_cb(oio_req *req) {
  int64_t now = oio_now();
  ASSERT(req != NULL);
  ASSERT(timeouts == expected);
  ASSERT(start_time < now);
  exit(0);
}

static void dummy_timeout_cb(oio_req *req) {
  /* Should never be called */
  FATAL("dummy_timer_cb should never be called");
}


TEST_IMPL(timeout) {
  oio_req *req;
  oio_req exit_req;
  oio_req dummy_req;
  int i;

  oio_init();

  start_time = oio_now();
  ASSERT(0 < start_time);

  /* Let 10 timers time out in 500 ms total. */
  for (i = 0; i < 10; i++) {
    req = (oio_req*)malloc(sizeof(*req));
    ASSERT(req != NULL);

    oio_req_init(req, NULL, timeout_cb);

    if (oio_timeout(req, i * 50) < 0) {
      FATAL("oio_timeout failed");
    }

    expected++;
  }

  /* The 11th timer exits the test and runs after 1 s. */
  oio_req_init(&exit_req, NULL, exit_timeout_cb);
  if (oio_timeout(&exit_req, 1000) < 0) {
    FATAL("oio_timeout failed");
  }

  /* The 12th timer should never run. */
  oio_req_init(&dummy_req, NULL, dummy_timeout_cb);
  if (oio_timeout(&dummy_req, 2000)) {
    FATAL("oio_timeout failed");
  }

  oio_run();

  FATAL("should never get here");
  return 2;
}
