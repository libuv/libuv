/* Copyright libuv contributors. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#include <stdint.h>

TEST_IMPL(buf_large) {
  uv_buf_t buf;

  buf = uv_buf_init(NULL, SIZE_MAX);
  ASSERT(buf.len == SIZE_MAX);
#ifdef _WIN32
  WSABUF* wbuf;

  wbuf = (WSABUF*) &buf;
  ASSERT(wbuf->len == buf.len);
#else
  struct iovec* iobuf;

  iobuf = (struct iovec*) &buf;
  ASSERT(iobuf->iov_len == buf.len);

  /* Verify that uv_buf_t is ABI-compatible with struct iovec. */
  ASSERT(sizeof(uv_buf_t) == sizeof(struct iovec));
  ASSERT(sizeof(&((uv_buf_t*) 0)->base) ==
         sizeof(((struct iovec*) 0)->iov_base));
  ASSERT(sizeof(&((uv_buf_t*) 0)->len) == sizeof(((struct iovec*) 0)->iov_len));
  ASSERT(offsetof(uv_buf_t, base) == offsetof(struct iovec, iov_base));
  ASSERT(offsetof(uv_buf_t, len) == offsetof(struct iovec, iov_len));
#endif

  return 0;
}
