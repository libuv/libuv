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
#define SLASH       '/'
#define BACK_SLASH  '\\'
#define SLASH_STR   "/"
  const char *frag;
  const char *frag_end;
  size_t      len;
  size_t      frag_len;
  int         frag_idx;
  char        c;

  if (!fragments
      || !*fragments /* there are no fragments to join */
      || !buf
      || !size)
    return -EINVAL;

  len = 0;

  /* 
   dont trim sep from beginning of first fragment (etc: /usr)
   */
  frag     = *fragments;
  frag_end = frag + strlen(frag) - 1;
  c        = *frag_end;

  while (c != '\0'
         && (c == SLASH || c == BACK_SLASH))
    c = *--frag_end;

  frag_end++;

  len += frag_len = frag_end - frag;
  if (*size < len)
    return -1;
  
  strncpy(buf, frag, frag_len);

  buf += frag_len;

  /* build path */
  frag_idx = 1;

  while ((frag = fragments[frag_idx++])) {
    frag_end = frag + strlen(frag) - 1;

    /* l-trim */
    while ((c = *frag) != '\0'
           && (c == SLASH || c == BACK_SLASH))
      frag++;

    /* r-trim */
    while ((c = *frag_end) != '\0'
           && (c == SLASH || c == BACK_SLASH))
      frag_end--;

    frag_end++;

    len += frag_len = frag_end - frag;
    if (*size < len)
      return -1;

    strncpy(buf++, SLASH_STR, 1);
    strncpy(buf, frag, frag_len);

    buf += frag_len;
  }

  memset(buf + 1, '\0', 1);
  *size = len;

  return 0;

#undef SLASH_STR
#undef BACK_SLASH
#undef SLASH
}
