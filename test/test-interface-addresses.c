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
#include <string.h>


TEST_IMPL(interface_addresses) {
  uv_interface_address_t* addrs;
  int count;

  ASSERT_OK(uv_interface_addresses(&addrs, &count));
  ASSERT_GE(count, 0);

  if (count > 0)
    ASSERT_NOT_NULL(addrs);

  uv_free_interface_addresses(addrs, count);
  return 0;
}


TEST_IMPL(interface_addresses2) {
  uv_interface_address2_t* addrs;
  int count;
  int i;
  static const char zeros[8] = {0};

  ASSERT_OK(uv_interface_addresses2(&addrs, &count));
  ASSERT_GE(count, 0);

  if (count > 0)
    ASSERT_NOT_NULL(addrs);

  for (i = 0; i < count; i++) {
    ASSERT_NOT_NULL(addrs[i].name);

    /* phys_addr_family must be a known value. */
    ASSERT_GE(addrs[i].phys_addr_family, UV_PHYS_ADDR_UNKNOWN);
    ASSERT_LE(addrs[i].phys_addr_family, UV_PHYS_ADDR_EUI64);

    /* address family must be AF_INET or AF_INET6. */
    ASSERT(addrs[i].address.address4.sin_family == AF_INET ||
           addrs[i].address.address4.sin_family == AF_INET6);

    if (addrs[i].phys_addr_family == UV_PHYS_ADDR_MAC48) {
      /* Bytes 6 and 7 must be zero for MAC-48 addresses. */
      ASSERT_EQ(addrs[i].phys_addr[6], 0);
      ASSERT_EQ(addrs[i].phys_addr[7], 0);
    }

    if (addrs[i].phys_addr_family == UV_PHYS_ADDR_UNKNOWN) {
      /* All bytes must be zero when the family is unknown. */
      ASSERT_MEM_EQ(addrs[i].phys_addr, zeros, 8);
    }
  }

  uv_free_interface_addresses2(addrs, count);
  return 0;
}


TEST_IMPL(interface_addresses2_consistency) {
  uv_interface_address_t* addrs;
  uv_interface_address2_t* addrs2;
  int count;
  int count2;
  int i;

  ASSERT_OK(uv_interface_addresses(&addrs, &count));
  ASSERT_OK(uv_interface_addresses2(&addrs2, &count2));

  /* Both APIs must return the same number of interfaces. */
  ASSERT_EQ(count, count2);

  for (i = 0; i < count; i++) {
    /* Names must match. */
    ASSERT_STR_EQ(addrs[i].name, addrs2[i].name);

    /* is_internal must match. */
    ASSERT_EQ(addrs[i].is_internal, addrs2[i].is_internal);

    /* First 6 bytes of phys_addr must match. */
    ASSERT_MEM_EQ(addrs[i].phys_addr, addrs2[i].phys_addr, 6);

    /* Address family must match. */
    ASSERT_EQ(addrs[i].address.address4.sin_family,
              addrs2[i].address.address4.sin_family);

    /* Address data must match. */
    if (addrs[i].address.address4.sin_family == AF_INET) {
      ASSERT_OK(memcmp(&addrs[i].address.address4,
                        &addrs2[i].address.address4,
                        sizeof(struct sockaddr_in)));
    } else {
      ASSERT_OK(memcmp(&addrs[i].address.address6,
                        &addrs2[i].address.address6,
                        sizeof(struct sockaddr_in6)));
    }

    /* Netmask must match. */
    if (addrs[i].netmask.netmask4.sin_family == AF_INET) {
      ASSERT_OK(memcmp(&addrs[i].netmask.netmask4,
                        &addrs2[i].netmask.netmask4,
                        sizeof(struct sockaddr_in)));
    } else if (addrs[i].netmask.netmask4.sin_family == AF_INET6) {
      ASSERT_OK(memcmp(&addrs[i].netmask.netmask6,
                        &addrs2[i].netmask.netmask6,
                        sizeof(struct sockaddr_in6)));
    }

    /* Broadcast must match. */
    if (addrs[i].broadcast.broadcast4.sin_family == AF_INET) {
      ASSERT_OK(memcmp(&addrs[i].broadcast.broadcast4,
                        &addrs2[i].broadcast.broadcast4,
                        sizeof(struct sockaddr_in)));
    }
  }

  uv_free_interface_addresses(addrs, count);
  uv_free_interface_addresses2(addrs2, count2);
  return 0;
}
