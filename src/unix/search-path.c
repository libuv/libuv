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

#include "internal.h"
#include "../strtok.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int uv__search_path(const char* prog, char* buf, size_t* buflen) {
  char abspath[UV__PATH_MAX];
  size_t abspath_size;
  char trypath[UV__PATH_MAX];
  char* cloned_path;
  char* path_env;
  char* token;
  char* itr;

  if (buf == NULL || buflen == NULL || *buflen == 0)
    return UV_EINVAL;

  /*
   * Possibilities for prog:
   * i) an absolute path such as: /home/user/myprojects/nodejs/node
   * ii) a relative path such as: ./node or ../myprojects/nodejs/node
   * iii) a bare filename such as "node", after exporting PATH variable
   *     to its location.
   */

  /* Case i) and ii) absolute or relative paths */
  if (strchr(prog, '/') != NULL) {
    if (realpath(prog, abspath) != abspath)
      return UV__ERR(errno);

    abspath_size = strlen(abspath);

    *buflen -= 1;
    if (*buflen > abspath_size)
      *buflen = abspath_size;

    memcpy(buf, abspath, *buflen);
    buf[*buflen] = '\0';

    return 0;
  }

  /* Case iii). Search PATH environment variable */
  cloned_path = NULL;
  token = NULL;
  path_env = getenv("PATH");

  if (path_env == NULL)
    return UV_EINVAL;

  cloned_path = uv__strdup(path_env);
  if (cloned_path == NULL)
    return UV_ENOMEM;

  token = uv__strtok(cloned_path, ":", &itr);
  while (token != NULL) {
    if (*token == '\0')
      snprintf(trypath, sizeof(trypath) - 1, "%s", prog);
    else
      snprintf(trypath, sizeof(trypath) - 1, "%s/%s", token, prog);
    if (realpath(trypath, abspath) == abspath) {
      /* Check the match is executable */
      if (access(abspath, X_OK) == 0) {
        abspath_size = strlen(abspath);

        *buflen -= 1;
        if (*buflen > abspath_size)
          *buflen = abspath_size;

        memcpy(buf, abspath, *buflen);
        buf[*buflen] = '\0';

        uv__free(cloned_path);
        return 0;
      }
    }
    token = uv__strtok(NULL, ":", &itr);
  }
  uv__free(cloned_path);

  /* Out of tokens (path entries), and no match found */
  return UV_EINVAL;
}
