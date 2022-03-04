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

#include <string.h>

#include "uv.h"
#include "task.h"

#include "uv/hash.h"

TEST_IMPL(hash) {
  uv_hash_t the_hash;
  size_t i;
  size_t j;
  int tests[] = { 1, 2, 3, 3000, 234, 398, 1000 };
  size_t tests_len = sizeof(tests) / sizeof(tests[0]);

  memset(&the_hash, 0, sizeof(uv_hash_t));
  for (i = 0; i < tests_len; i++)
    ASSERT_EQ(uv_hash_insert(&the_hash, &tests[i], &tests[i]), 0);

  for (i = 0; i < tests_len; i++)
    ASSERT_PTR_EQ(uv_hash_find(&the_hash, &tests[i]), &tests[i]);

  ASSERT_NULL(uv_hash_find(&the_hash, &i));
  ASSERT_NULL(uv_hash_find(&the_hash, &the_hash));
  ASSERT_NULL(uv_hash_find(&the_hash, &tests_len));

  for (i = 0; i < 2; i++)
    for (j = 0; j < tests_len; j++)
      ASSERT_PTR_EQ(uv_hash_remove(&the_hash,
                                   &tests[j]),
                                   !(i % 2) ? &tests[j] : NULL);

  // Insert everyone again...
  for (i = 0; i < tests_len; i++)
    ASSERT_EQ(uv_hash_insert(&the_hash, &tests[i], &tests[i]), 0);

  uv_hash_clear(&the_hash);

  return 0;
}
