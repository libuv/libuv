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
#include "task.h"

#define GOOD_ADDR_LIST(X)                                                     \
    X("::")                                                                   \
    X("::1")                                                                  \
    X("fe80::1")                                                              \
    X("fe80::")                                                               \
    X("fe80::2acf:daff:fedd:342a")                                            \
    X("fe80:0:0:0:2acf:daff:fedd:342a")                                       \
    X("fe80:0:0:0:2acf:daff:1.2.3.4")                                         \

#define BAD_ADDR_LIST(X)                                                      \
    X(":::1")                                                                 \
    X("abcde::1")                                                             \
    X("fe80:0:0:0:2acf:daff:fedd:342a:5678")                                  \
    X("fe80:0:0:0:2acf:daff:abcd:1.2.3.4")                                    \
    X("fe80:0:0:2acf:daff:1.2.3.4.5")                                         \

#define TEST_GOOD(ADDR)                                                       \
    ASSERT(uv_inet_pton(AF_INET6, ADDR, &addr).code == UV_OK);                \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%en1", &addr).code == UV_OK);         \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%%%%", &addr).code == UV_OK);         \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%en1:1.2.3.4", &addr).code == UV_OK); \

#define TEST_BAD(ADDR)                                                        \
    ASSERT(uv_inet_pton(AF_INET6, ADDR, &addr).code != UV_OK);                \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%en1", &addr).code != UV_OK);         \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%%%%", &addr).code != UV_OK);         \
    ASSERT(uv_inet_pton(AF_INET6, ADDR "%en1:1.2.3.4", &addr).code != UV_OK); \

TEST_IMPL(ip6_pton) {
  struct in6_addr addr;

  GOOD_ADDR_LIST(TEST_GOOD)
  BAD_ADDR_LIST(TEST_BAD)

  MAKE_VALGRIND_HAPPY();
  return 0;
}

#undef GOOD_ADDR_LIST
#undef BAD_ADDR_LIST
