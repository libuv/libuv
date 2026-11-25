/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#include <fcntl.h>
#include <string.h>


static int unlink_close_cb_called;


static void unlink_close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  unlink_close_cb_called++;
}


/* uv_pipe_bind() remembers the path that uv_close() unlinks. A relative path
 * has to be anchored at the working directory when it is stored, because
 * unlink() resolves it against the working directory that happens to be in
 * effect at close time - after a uv_chdir() that is a different directory, and
 * therefore a different file system entity: the socket leaks and whatever the
 * old name points to now is destroyed instead.
 */
TEST_IMPL(pipe_unlink_after_chdir) {
#if defined(_WIN32)
  RETURN_SKIP("Windows named pipes are not affected by the working directory.");
#else
  static const char dirname[] = "uv-test-chdir-dir";
  char decoyname[sizeof(dirname) + sizeof(TEST_PIPENAME)];
  char cwd[4096];
  uv_loop_t* loop;
  uv_pipe_t pipe;
  uv_fs_t req;
  size_t size;
  uv_file file;
  int r;

  loop = uv_default_loop();
  unlink_close_cb_called = 0;

  size = sizeof(cwd);
  ASSERT_OK(uv_cwd(cwd, &size));

  snprintf(decoyname, sizeof(decoyname), "%s/%s", dirname, TEST_PIPENAME);

  /* Tidy up after a previous run that was interrupted half way through. */
  uv_fs_unlink(NULL, &req, decoyname, NULL);
  uv_fs_req_cleanup(&req);
  uv_fs_rmdir(NULL, &req, dirname, NULL);
  uv_fs_req_cleanup(&req);
  uv_fs_unlink(NULL, &req, TEST_PIPENAME, NULL);
  uv_fs_req_cleanup(&req);

  ASSERT_OK(uv_fs_mkdir(NULL, &req, dirname, 0755, NULL));
  uv_fs_req_cleanup(&req);

  /* The innocent bystander: a plain file with the name of the socket, living
   * in the directory that the working directory becomes before the close. */
  file = uv_fs_open(NULL,
                    &req,
                    decoyname,
                    O_CREAT | O_WRONLY | O_TRUNC,
                    0644,
                    NULL);
  ASSERT_GT(file, -1);
  uv_fs_req_cleanup(&req);
  ASSERT_OK(uv_fs_close(NULL, &req, file, NULL));
  uv_fs_req_cleanup(&req);

  ASSERT_OK(uv_pipe_init(loop, &pipe, 0));
  ASSERT_OK(uv_pipe_bind(&pipe, TEST_PIPENAME));

  ASSERT_OK(uv_chdir(dirname));

  uv_close((uv_handle_t*) &pipe, unlink_close_cb);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
  ASSERT_EQ(unlink_close_cb_called, 1);

  ASSERT_OK(uv_chdir(cwd));

  ASSERT_EQ(UV_ENOENT, uv_fs_lstat(NULL, &req, TEST_PIPENAME, NULL));
  uv_fs_req_cleanup(&req);
  ASSERT_OK(uv_fs_lstat(NULL, &req, decoyname, NULL));
  uv_fs_req_cleanup(&req);

  ASSERT_OK(uv_fs_unlink(NULL, &req, decoyname, NULL));
  uv_fs_req_cleanup(&req);
  ASSERT_OK(uv_fs_rmdir(NULL, &req, dirname, NULL));
  uv_fs_req_cleanup(&req);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}
