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
int
uv_path_join(char **fragments,
             char *buf,
             size_t *size) {
#define CHR_SLASH       '/'
#define CHR_BACK_SLASH  '\\'
#define CHR_COLON       ':'
#define STR_SLASH       "/"
#define APPEND_SLASH \
  do { \
    strncpy(buf++, STR_SLASH, 1); \
    len++; \
  } while (0)

  const char *frag;
  const char *frag_end;
  size_t      len;
  size_t      frag_len;
  int         frag_idx;
  int         frag_idx_v;
  char        c;

  if (!fragments
      || !*fragments /* there are no fragments to join */
      || !buf
      || !size)
    return -EINVAL;

  frag_idx_v = frag_idx = len = 0;

  /* build path */
  while ((frag = fragments[frag_idx++])) {
    frag_len = strlen(frag);
    frag_end = frag + frag_len - 1;

    if (frag_len == 0)
      continue;

    /* starts with slash e.g. /usr */
    c = *frag;
    if (frag_idx_v == 0
        && (c == CHR_SLASH || c == CHR_BACK_SLASH)) {
      APPEND_SLASH;
      frag++;

      /* avoid extra slash after non-protocol frag 
         like /http:/ -> /http:// 
       */
      frag_idx_v++;
    }

    /* l-trim */
    while ((c = *frag) != '\0'
           && (c == CHR_SLASH || c == CHR_BACK_SLASH))
      frag++;

    /* r-trim */
    while ((c = *frag_end) != '\0'
           && (c == CHR_SLASH || c == CHR_BACK_SLASH))
      frag_end--;

    /* only slashes */
    if (!*frag) {
      if (len == 0)
        APPEND_SLASH;

      continue;
    }

    if (len > 1 && *(buf - 1) != CHR_SLASH)
      APPEND_SLASH;

    /* protocol e.g. http://, tcp:// */
    if (frag_idx_v == 1 && *(buf - 2) == CHR_COLON)
      APPEND_SLASH;

    frag_len = ++frag_end - frag;
    len += frag_len;

    if (*size < len)
      return -1;

    strncpy(buf, frag, frag_len);

    buf += frag_len;
    frag_idx_v++;
  };

  memset(buf + 1, '\0', 1);
  *size = len;

  return 0;

#undef APPEND_SLASH
#undef STR_SLASH
#undef CHR_COLON
#undef CHR_BACK_SLASH
#undef CHR_SLASH
}
