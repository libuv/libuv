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
#include "uv-common.h"
#include <stdlib.h>

/* The windows implementation does not have the same structure layout as
 * the unix implementation (nbufs is not directly inside req but is
 * contained in a nested union/struct) so this function locates it.
*/
static unsigned int* uv__get_nbufs(uv_fs_t* req) {
#ifdef _WIN32
  return &req->fs.info.nbufs;
#else
  return &req->nbufs;
#endif
}

/* uv_fs_scandir() uses the system allocator to allocate memory on non-Windows
 * systems. So, the memory should be released using free(). On Windows,
 * uv__malloc() is used, so use uv__free() to free memory.
*/
#ifdef _WIN32
# define uv__fs_scandir_free uv__free
#else
# define uv__fs_scandir_free free
#endif

void uv__fs_scandir_cleanup(uv_fs_t* req) {
  uv__dirent_t** dents;
  unsigned int* nbufs;
  unsigned int i;
  unsigned int n;

  if (req->result >= 0) {
    dents = req->ptr;
    nbufs = uv__get_nbufs(req);

    i = 0;
    if (*nbufs > 0)
      i = *nbufs - 1;

    n = (unsigned int) req->result;
    for (; i < n; i++)
      uv__fs_scandir_free(dents[i]);
  }

  uv__fs_scandir_free(req->ptr);
  req->ptr = NULL;
}


int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent) {
  uv__dirent_t** dents;
  uv__dirent_t* dent;
  unsigned int* nbufs;

  /* Check to see if req passed */
  if (req->result < 0)
    return req->result;

  /* Ptr will be null if req was canceled or no files found */
  if (!req->ptr)
    return UV_EOF;

  nbufs = uv__get_nbufs(req);
  assert(nbufs);

  dents = req->ptr;

  /* Free previous entity */
  if (*nbufs > 0)
    uv__fs_scandir_free(dents[*nbufs - 1]);

  /* End was already reached */
  if (*nbufs == (unsigned int) req->result) {
    uv__fs_scandir_free(dents);
    req->ptr = NULL;
    return UV_EOF;
  }

  dent = dents[(*nbufs)++];

  ent->name = dent->d_name;
  ent->type = uv__fs_get_dirent_type(dent);

  return 0;
}

uv_dirent_type_t uv__fs_get_dirent_type(uv__dirent_t* dent) {
  uv_dirent_type_t type;

#ifdef HAVE_DIRENT_TYPES
  switch (dent->d_type) {
    case UV__DT_DIR:
      type = UV_DIRENT_DIR;
      break;
    case UV__DT_FILE:
      type = UV_DIRENT_FILE;
      break;
    case UV__DT_LINK:
      type = UV_DIRENT_LINK;
      break;
    case UV__DT_FIFO:
      type = UV_DIRENT_FIFO;
      break;
    case UV__DT_SOCKET:
      type = UV_DIRENT_SOCKET;
      break;
    case UV__DT_CHAR:
      type = UV_DIRENT_CHAR;
      break;
    case UV__DT_BLOCK:
      type = UV_DIRENT_BLOCK;
      break;
    default:
      type = UV_DIRENT_UNKNOWN;
  }
#else
  type = UV_DIRENT_UNKNOWN;
#endif

  return type;
}

void uv__fs_readdir_cleanup(uv_fs_t* req) {
  uv_dir_t* dir;
  uv_dirent_t* dirents;
  int i;

  if (req->ptr == NULL)
    return;

  dir = req->ptr;
  dirents = dir->dirents;
  req->ptr = NULL;

  if (dirents == NULL)
    return;

  for (i = 0; i < req->result; ++i) {
    uv__free((char*) dirents[i].name);
    dirents[i].name = NULL;
  }
}


