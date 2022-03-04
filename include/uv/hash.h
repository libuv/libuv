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

#ifndef UV_HASH_H_
#define UV_HASH_H_

#include <stdlib.h>
#include <errno.h>

#include "errno.h"
#include "tree.h"

#if EDOM > 0
# define UV__ERR(x) (-(x))
#else
# define UV__ERR(x) (x)
#endif

#define UV__HASH_BUCKET_FACTOR 8
#define UV__HASH_BUCKET_SIZE 1 << UV__HASH_BUCKET_FACTOR
#define UV__HASH_SEED 10
#define UV__HASH_RBTREE_MASK 0xFFFF

struct uv_hash_head_s {
  RB_ENTRY(uv_hash_head_s) entry;
  struct uv_hash_element_s* rbh_root;
  int hash;
};

struct uv_hash_element_s {
  RB_ENTRY(uv_hash_element_s) entry;
  const void* key;
  void* data;
};

struct uv_hash_buckets_s {
  struct uv_hash_head_s* rbh_root;
};

struct uv_hash_s {
  struct uv_hash_buckets_s buckets[UV__HASH_BUCKET_SIZE];
  unsigned int population;
};

typedef struct uv_hash_element_s uv_hash_element_t;
typedef struct uv_hash_head_s uv_hash_head_t;
typedef struct uv_hash_s uv_hash_t;

static inline int compare_hash_head_tree_element(const uv_hash_head_t* a,
                                                 const uv_hash_head_t* b) {
  if (a->hash < b->hash) return -1;
  if (a->hash > b->hash) return 1;
  return 0;
}

static inline int compare_hash_tree_element(const uv_hash_element_t* a,
                                            const uv_hash_element_t* b) {
  if (a->key < b->key) return -1;
  if (a->key > b->key) return 1;
  return 0;
}

RB_GENERATE_STATIC(uv_hash_buckets_s,
                   uv_hash_head_s,
                   entry,
                   compare_hash_head_tree_element)

RB_GENERATE_STATIC(uv_hash_head_s,
                   uv_hash_element_s,
                   entry,
                   compare_hash_tree_element)

#if defined(_WIN64) || \
  defined(__LP64__) || \
  defined(_LP64) || \
  defined(__powerpc64__) || \
  defined(__ppc64__) || \
  defined(__PPC64__) || \
  defined(__mips64) || \
  defined(__mips64__) || \
  defined(__mips_n64)

static inline int calc_hash(const void* pkey) {
   unsigned long long int key = (unsigned long long int) pkey;
   key  = ~key + (key << 18);
   key ^= key >> 31;
   key *= 21 ^ UV__HASH_SEED;
   key ^= key >> 11;
   key += key << 6;
   key ^= key >> 22;
   return (int) key;
}
#else
static inline int calc_hash(const void* pkey) {
   unsigned int key = (unsigned int) pkey;
   key  = ~key + (key << 15);
   key ^= key >> 12;
   key += key << 2;
   key ^= key >> 4;
   key *= 2057 ^ UV__HASH_SEED;
   key ^= key >> 16;
   return (int) key;
}
#endif

static inline int uv_hash_insert(uv_hash_t* hash, const void* key, void* data) {
  uv_hash_head_t* head;
  uv_hash_head_t head_finder;
  uv_hash_element_t* new_element;
  int key_hash;
  int hash_num;
  int is_new_head;

  key_hash = calc_hash(key);
  hash_num = key_hash & ((UV__HASH_BUCKET_SIZE) - 1);
  key_hash >>= UV__HASH_BUCKET_FACTOR;
  key_hash &= UV__HASH_RBTREE_MASK;

  head_finder.hash = key_hash;
  head = RB_FIND(uv_hash_buckets_s, &hash->buckets[hash_num], &head_finder);

  if (!head) {
    head = calloc(1, sizeof(uv_hash_head_t));
    if (!head) return UV__ENOMEM;
    head->hash = key_hash;
    is_new_head = 1;
    RB_INSERT(uv_hash_buckets_s, &hash->buckets[hash_num], head);
  } else
    is_new_head = 0;

  new_element = calloc(1, sizeof(uv_hash_element_t));
  if (!new_element) {
    if (is_new_head) {
      RB_REMOVE(uv_hash_buckets_s, &hash->buckets[hash_num], head);
      free(head);
    }
    return UV__ENOMEM;
  }
  new_element->key = key;
  new_element->data = data;
  RB_INSERT(uv_hash_head_s, head, new_element);
  hash->population++;
  return 0;
}

static inline uv_hash_element_t* uv_hash_find_hash_element(uv_hash_t* hash,
                                                           const void* key,
                                                           uv_hash_head_t** h,
                                                           int* out_key_hash) {
  uv_hash_head_t head_finder;
  uv_hash_head_t* head;
  uv_hash_element_t element_finder;
  int key_hash;
  int rb_hash;

  key_hash = calc_hash(key);
  rb_hash = (key_hash >> UV__HASH_BUCKET_FACTOR) & UV__HASH_RBTREE_MASK;
  key_hash &= ((UV__HASH_BUCKET_SIZE) - 1);

  head_finder.hash = rb_hash;
  head = RB_FIND(uv_hash_buckets_s, &hash->buckets[key_hash], &head_finder);
  if (!head) return NULL;

  element_finder.key = key;
  if (h) *h = head;
  if (out_key_hash) *out_key_hash = key_hash;
  return RB_FIND(uv_hash_head_s, head, &element_finder);
}

static inline void* uv_hash_find(uv_hash_t* hash, const void* key) {
  uv_hash_element_t* element;

  if (!hash->population) return NULL;
  element = uv_hash_find_hash_element(hash, key, NULL, NULL);
  if (!element) return NULL;
  return element->data;
}

static inline void* uv_hash_remove(uv_hash_t* hash, const void* key) {
  int key_hash;
  uv_hash_element_t* element;
  uv_hash_head_t* head;
  void* data;

  if (!hash->population) return NULL;

  element = uv_hash_find_hash_element(hash, key, &head, &key_hash);
  if (!element) return NULL;

  RB_REMOVE(uv_hash_head_s, head, element);
  if (RB_EMPTY(head)) {
    RB_REMOVE(uv_hash_buckets_s, &hash->buckets[key_hash], head);
    free(head);
  }
  data = element->data;
  hash->population--;
  free(element);
  return data;
}

static inline void uv_hash_clear(uv_hash_t* hash) {
  size_t i;
  size_t buckets_len;
  uv_hash_head_t* tmp_hash_head_itr;
  uv_hash_head_t* hash_head_itr;
  uv_hash_element_t* element_itr;
  uv_hash_element_t* tmp_element_itr;

  if (!hash->population) return;

  buckets_len = sizeof(hash->buckets) / sizeof(hash->buckets[0]);
  for (i = 0; i < buckets_len; i++) {
    RB_FOREACH_SAFE(hash_head_itr, uv_hash_buckets_s,
                    &hash->buckets[i], tmp_hash_head_itr) {
      RB_REMOVE(uv_hash_buckets_s, &hash->buckets[i], hash_head_itr);
      RB_FOREACH_SAFE(element_itr, uv_hash_head_s,
                      hash_head_itr, tmp_element_itr) {
        RB_REMOVE(uv_hash_head_s, hash_head_itr, element_itr);
        free(element_itr);
      }
      free(hash_head_itr);
    }
  }
  hash->population = 0;
}

#endif
