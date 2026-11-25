/* Copyright libuv project contributors. All rights reserved.
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

#include <stdio.h>
#include <string.h>

int uv__cgroup1_find_cpu_controller(const char* cgroup,
                                    char* path,
                                    size_t path_size) {
  const char* controller;
  const char* controller_end;
  const char* controller_start;
  const char* group_end;
  const char* group_start;
  const char* line_end;
  const char* line_start;
  size_t controller_list_size;
  size_t controller_size;
  size_t group_size;
  int n;

  for (line_start = cgroup; *line_start != '\0'; line_start = line_end + 1) {
    line_end = strchr(line_start, '\n');
    if (line_end == NULL)
      line_end = line_start + strlen(line_start);

    controller_start = memchr(line_start, ':', line_end - line_start);
    if (controller_start == NULL)
      goto next_line;
    controller_start++;

    controller_end = memchr(controller_start,
                            ':',
                            line_end - controller_start);
    if (controller_end == NULL)
      goto next_line;

    for (controller = controller_start; controller < controller_end;) {
      const char* comma;

      comma = memchr(controller, ',', controller_end - controller);
      if (comma == NULL)
        comma = controller_end;
      controller_size = comma - controller;
      if (controller_size == sizeof("cpu") - 1 &&
          memcmp(controller, "cpu", controller_size) == 0)
        break;
      controller = comma + 1;
    }

    if (controller >= controller_end)
      goto next_line;

    group_start = controller_end + 1;
    if (group_start < line_end && *group_start == '/')
      group_start++;
    group_end = line_end;

    controller_list_size = controller_end - controller_start;
    group_size = group_end - group_start;
    if (group_size == 0)
      n = snprintf(path,
                   path_size,
                   "%.*s",
                   (int) controller_list_size,
                   controller_start);
    else
      n = snprintf(path,
                   path_size,
                   "%.*s/%.*s",
                   (int) controller_list_size,
                   controller_start,
                   (int) group_size,
                   group_start);

    if (n < 0 || (size_t) n >= path_size)
      return UV_ENOBUFS;
    return 0;

next_line:
    if (*line_end == '\0')
      break;
  }

  return UV_EIO;
}
