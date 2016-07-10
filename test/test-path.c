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

#include <limits.h>
#include <string.h>

TEST_IMPL(path_combine) {
  char      **fragments;
  char       *valid_path;
  char        path[PATH_MAX];
  char        invalid_path_buf[1];
  size_t      path_len;
  size_t      path_oldlen;
  int         r;

  valid_path  = "/usr/local/bin";
  path_oldlen = path_len = sizeof(path);
  fragments   = (char *[]){"/usr/", "/local/", "\\bin/", NULL};

  r = uv_path_join(fragments,
                   path,
                   &path_len);

  ASSERT(strcmp(path, valid_path) == 0);
  ASSERT(path_oldlen >= path_len);
  ASSERT(r == 0);

  valid_path = "http://docs.libuv.org/en/v1.x/fs.html";
  fragments  = (char *[]){"http://docs.libuv.org/en",
                          "v1.x/",
                          "/fs.html", NULL};
  path_len = path_oldlen;

  r = uv_path_join(fragments,
                   path,
                   &path_len);

  ASSERT(strcmp(path, valid_path) == 0);
  ASSERT(path_oldlen >= path_len);
  ASSERT(r == 0);

  path_len = sizeof(invalid_path_buf);
  r = uv_path_join(fragments,
                   invalid_path_buf,
                   &path_len);

  ASSERT(r == -1);

  r = uv_path_join((char *[]){NULL},
                   invalid_path_buf,
                   &path_len);

  ASSERT(r == -EINVAL);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
