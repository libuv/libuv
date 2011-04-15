#include "test.h"

TEST_IMPL(fail_always) {
  /* This test always fails. It is used to test the test runner. */
  FATAL("Yes, it always fails")
  return 2;
}