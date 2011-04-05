TEST_DECLARE (echo_server)
TEST_DECLARE (ping_pong)
TEST_DECLARE (close_cb_stack);
TEST_DECLARE (pass_always)
TEST_DECLARE (fail_always)

TEST_LIST_START
  TEST_ENTRY  (ping_pong)
  TEST_HELPER (ping_pong, echo_server)

  TEST_ENTRY  (close_cb_stack)

  TEST_ENTRY  (fail_always)

  TEST_ENTRY  (pass_always)
TEST_LIST_END