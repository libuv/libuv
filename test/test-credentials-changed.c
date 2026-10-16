/* Copyright the libuv project contributors. All rights reserved.
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

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>

struct passwd* pw;

void open_cb(uv_fs_t* req) {
  ASSERT_EQ(UV_EACCES, req->result);
  uv_fs_req_cleanup(req);
}

void stat_cb(uv_fs_t* req) {
  /* become the "nobody" user. */
  ASSERT_OK(setuid(pw->pw_uid));

  uv_credentials_changed();

  uv_fs_req_cleanup(req);

  uv_fs_open(req->loop,
             req, "/etc/shadow",
             UV_FS_O_CREAT | UV_FS_O_RDWR,
             0666,
             open_cb);
}

TEST_FS_IMPL(credentials_changed) {
  uv_loop_t* loop;
  uv_fs_t req;

  uv_uid_t uid;
  /* if not root, then this will fail. */
  uid = getuid();
  if (uid != 0)
    RETURN_SKIP("It should be run as root user");

  pw = getpwnam("nobody");
  if (pw == NULL)
    RETURN_SKIP("'nobody' user not available");

  loop = uv_default_loop();

  /* This first request is only to make sure the io_uring instance is
   * initialized. */
  uv_fs_stat(loop, &req, ".", stat_cb);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
#endif
