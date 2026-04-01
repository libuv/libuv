/*
 * Fuzz target for libuv IDNA (Internationalized Domain Names) processing.
 *
 * Tests uv__idna_toascii which converts international domain names to ASCII,
 * and uv__utf8_decode1 which decodes UTF-8 sequences. These functions process
 * untrusted hostname input from DNS resolution.
 */

#include "uv.h"
#include "../src/idna.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 4096) return 0;

    /* Ensure null-terminated input */
    char *input = (char *)malloc(size + 1);
    if (!input) return 0;
    memcpy(input, data, size);
    input[size] = '\0';

    /* Test 1: IDNA toascii conversion */
    char output[256];
    uv__idna_toascii(input, input + size, output, output + sizeof(output));

    /* Test 2: UTF-8 decoding */
    const char *p = input;
    const char *pe = input + size;
    while (p < pe) {
        uv__utf8_decode1(&p, pe);
    }

    free(input);
    return 0;
}
