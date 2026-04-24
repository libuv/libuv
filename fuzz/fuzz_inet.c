/*
 * Fuzz target for libuv IP address parsing.
 *
 * Tests uv_inet_pton, uv_ip4_addr, uv_ip6_addr which parse untrusted
 * IP address strings from network input.
 */

#include "uv.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 1024) return 0;

    /* Ensure null-terminated input */
    char *input = (char *)malloc(size + 1);
    if (!input) return 0;
    memcpy(input, data, size);
    input[size] = '\0';

    /* Test 1: IPv4 address parsing */
    struct sockaddr_in addr4;
    uv_ip4_addr(input, 80, &addr4);

    /* Test 2: IPv6 address parsing */
    struct sockaddr_in6 addr6;
    uv_ip6_addr(input, 80, &addr6);

    /* Test 3: inet_pton IPv4 */
    unsigned char buf4[4];
    uv_inet_pton(AF_INET, input, buf4);

    /* Test 4: inet_pton IPv6 */
    unsigned char buf6[16];
    uv_inet_pton(AF_INET6, input, buf6);

    /* Test 5: inet_ntop round-trip (if parse succeeded) */
    char ntop_buf[64];
    if (uv_inet_pton(AF_INET, input, buf4) == 0) {
        uv_inet_ntop(AF_INET, buf4, ntop_buf, sizeof(ntop_buf));
    }
    if (uv_inet_pton(AF_INET6, input, buf6) == 0) {
        uv_inet_ntop(AF_INET6, buf6, ntop_buf, sizeof(ntop_buf));
    }

    free(input);
    return 0;
}
