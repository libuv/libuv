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
#include "uv-common.h"

#include <string.h>

UV_EXTERN
void
uv_path_combine(const char *path,
                const char *component,
                char *buf) {
#define SLASH       '/'
#define BACK_SLASH  '\\'
#define SLASH_STR   "/"

  char       *iterPath;
  const char *iterComponent;

  strcpy(buf, path);

  iterPath      = buf + strlen(path) - 1;
  iterComponent = component;

  while (*iterPath != '\0'
         && (*iterPath == SLASH || *iterPath == BACK_SLASH))
    iterPath--;

  iterPath++;

  while (*iterComponent != '\0'
         && (*iterComponent == SLASH || *iterComponent == BACK_SLASH))
    iterComponent++;

  strcpy(iterPath++, SLASH_STR);
  strcat(iterPath, iterComponent);
  memset(iterPath + strlen(iterComponent), '\0', 1);

#undef SLASH_STR
#undef BACK_SLASH
#undef SLASH
}
