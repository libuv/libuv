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
  const char *comp;
  char        homedir[PATH_MAX];
  char        path[PATH_MAX];
  char        path2[PATH_MAX];
  size_t      homedir_len;
  int         r;

  homedir_len = sizeof(homedir);

  r = uv_os_homedir(homedir,
                    &homedir_len);

  ASSERT(r == 0);

  comp = "/Downloads";

  strcpy(path2, homedir);
  strcat(path2, comp);

  uv_path_combine(homedir,
                  comp,
                  path);

  ASSERT(strcmp(path, path2) == 0);

  comp = "///Downloads";

  uv_path_combine(homedir,
                  comp,
                  path);

  ASSERT(strcmp(path, path2) == 0);

  comp = "\\Downloads";

  uv_path_combine(homedir,
                  comp,
                  path);

  ASSERT(strcmp(path, path2) == 0);

  strcat(homedir, "////");

  uv_path_combine(homedir,
                  comp,
                  path);

  ASSERT(strcmp(path, path2) == 0);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
