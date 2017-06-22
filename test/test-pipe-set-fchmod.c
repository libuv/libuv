#include "uv.h"
#include "task.h"

TEST_IMPL(pipe_set_fchmod) {
  uv_pipe_t pipe_handle;
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &pipe_handle, 0);
  ASSERT(r == 0);

  r = uv_pipe_bind(&pipe_handle, TEST_PIPENAME);
  ASSERT(r == 0);

  /* No easy way to test if this works, we will only make sure that */
  /* the call is successful. */
  r = uv_pipe_chmod(&pipe_handle, UV_READABLE);
  ASSERT(r == 0);

  r = uv_pipe_chmod(&pipe_handle, UV_WRITABLE);
  ASSERT(r == 0);

  r = uv_pipe_chmod(&pipe_handle, UV_WRITABLE | UV_READABLE);
  ASSERT(r == 0);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
