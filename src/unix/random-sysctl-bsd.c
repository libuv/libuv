/* Copyright libuv contributors. All rights reserved.
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
#include "internal.h"

#include <errno.h>
#include <string.h>

#include <sys/sysctl.h>
#include <unistd.h>

int uv__random_sysctl(void* buf, size_t buflen) {
  static int name[] = {CTL_KERN, KERN_ARND};
  unsigned char rbytes[32];
  char* p;
  char* pe;
  size_t n = sizeof(rbytes);

  p = buf;
  pe = p + buflen;

  while (p < pe) {
    if (sysctl(name, ARRAY_SIZE(name), rbytes, &n, NULL, 0) == -1)
      return UV__ERR(errno);

    if (n != sizeof(rbytes))
      return UV_EIO;  /* Can't happen. */

    n = pe - p;
    if (n > sizeof(rbytes))
      n = sizeof(rbytes);

    memcpy(p, rbytes, n);
    p += n;
  }

  return 0;
}
