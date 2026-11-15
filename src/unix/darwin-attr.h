/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#ifndef UV_DARWIN_ATTR_H_
#define UV_DARWIN_ATTR_H_

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/attr.h>

struct uv__darwin_path_attr {
  uint32_t length;
  attrreference_t relative;
  char data[PATH_MAX];
};

static inline const char* uv__darwin_path_attr_string(
    const struct uv__darwin_path_attr* attrs) {
  size_t offset;
  size_t length;
  const char* path;

  /* A successful attribute query can return a truncated string. */
  if (attrs->length < offsetof(struct uv__darwin_path_attr, data) ||
      attrs->length > sizeof(*attrs) ||
      attrs->relative.attr_dataoffset < (int) sizeof(attrs->relative))
    return NULL;

  offset = offsetof(struct uv__darwin_path_attr, relative) +
           (size_t) attrs->relative.attr_dataoffset;
  length = attrs->relative.attr_length;
  if (offset >= attrs->length || length == 0 || length > attrs->length - offset)
    return NULL;

  path = (const char*) attrs + offset;
  if (path[0] != '/' || path[length - 1] != '\0')
    return NULL;

  return path;
}

#endif /* UV_DARWIN_ATTR_H_ */
