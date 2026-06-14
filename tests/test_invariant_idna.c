#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration of the function under test from src/idna.c */
int uv_idna_toascii(const char *input, char *output, size_t output_len);

START_TEST(test_idna_buffer_overflow_protection)
{
    /* Invariant: Buffer reads/writes never exceed declared length during IDNA encoding */
    
    /* Payloads: exploit case (many Unicode chars), boundary case, valid input */
    const char *payloads[] = {
        /* Exploit: 100 combining diacritics that expand significantly in punycode */
        "a\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81"
        "\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81"
        "\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81"
        "\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81"
        "\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81\xCC\x81",
        /* Boundary: single high Unicode character */
        "\xE2\x98\x83",
        /* Valid: simple ASCII domain */
        "example.com"
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        char output[256];
        memset(output, 0xAA, sizeof(output));
        
        /* Call the actual production function */
        int result = uv_idna_toascii(payloads[i], output, sizeof(output) - 1);
        
        /* Invariant checks:
         * 1. Function returns without crashing (no segfault/abort)
         * 2. Output buffer is not overwritten beyond declared size
         * 3. Either succeeds (result >= 0) or fails gracefully (result < 0)
         */
        ck_assert(result >= -1);
        
        /* Verify no overflow: check guard bytes beyond output_len */
        ck_assert_int_eq(output[sizeof(output) - 1], 0xAA);
        
        /* If successful, output must be null-terminated within bounds */
        if (result >= 0) {
            ck_assert(strlen(output) < sizeof(output));
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_idna_buffer_overflow_protection);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}