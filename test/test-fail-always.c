#include "test.h"

TEST_IMPL(fail_always) {
  /* This test always fails. It is used to test the test runner. */
  assert("Yes, it always fails" && 0);
  return 1;
}