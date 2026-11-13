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
#include "task.h"

#ifndef _WIN32

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#define UV_UNIX_INTERNAL_H_
#if defined(PATH_MAX)
# define UV__PATH_MAX PATH_MAX
#else
# define UV__PATH_MAX 8192
#endif
#if EDOM > 0
# define UV__ERR(value) (-(value))
#else
# define UV__ERR(value) (value)
#endif

static char* test_strdup(const char* value) {
  char* copy;
  size_t size;

  size = strlen(value) + 1;
  copy = malloc(size);
  if (copy != NULL)
    memcpy(copy, value, size);
  return copy;
}

#define uv__strdup test_strdup
#define uv__free free
#define uv__strtok test_uv__strtok
#include "../src/strtok.c"
#define uv__search_path test_uv__search_path
#include "../src/unix/search-path.c"
#undef uv__search_path
#undef uv__strtok
#undef uv__free
#undef uv__strdup

#endif

TEST_IMPL(search_path_empty_component) {
#ifdef _WIN32
  RETURN_SKIP("PATH uses different semantics on Windows");
#else
  static const char* const paths[] = {
    ":/does/not/exist",
    "/does/not/exist:",
    "/does/not/exist::/also/does/not/exist"
  };
  char buffer[UV__PATH_MAX];
  char cwd[UV__PATH_MAX];
  char executable[UV__PATH_MAX];
  char expected[UV__PATH_MAX];
  char template[] = "uv-search-path-XXXXXX";
  char* directory;
  char* old_path;
  const char* old_path_value;
  size_t buffer_size;
  size_t i;
  int fd;

  ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
  old_path_value = getenv("PATH");
  old_path = old_path_value == NULL ? NULL : test_strdup(old_path_value);
  ASSERT(old_path_value == NULL || old_path != NULL);

  directory = mkdtemp(template);
  ASSERT_NOT_NULL(directory);
  snprintf(executable, sizeof(executable), "%s/probe", directory);
  fd = open(executable, O_CREAT | O_WRONLY, 0700);
  ASSERT_GE(fd, 0);
  ASSERT_OK(close(fd));
  ASSERT_NOT_NULL(realpath(executable, expected));
  ASSERT_OK(chdir(directory));

  for (i = 0; i < ARRAY_SIZE(paths); i++) {
    ASSERT_OK(setenv("PATH", paths[i], 1));
    buffer_size = sizeof(buffer);
    ASSERT_OK(test_uv__search_path("probe", buffer, &buffer_size));
    ASSERT_STR_EQ(buffer, expected);
  }

  if (old_path == NULL)
    ASSERT_OK(unsetenv("PATH"));
  else
    ASSERT_OK(setenv("PATH", old_path, 1));
  ASSERT_OK(chdir(cwd));
  ASSERT_OK(unlink(expected));
  ASSERT_OK(rmdir(directory));
  free(old_path);
  return 0;
#endif
}
