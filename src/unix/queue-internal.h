/* Copyright (c) 2015, Andrey Mazo <andrey.mazo@fidelissecurity.com>
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

#ifndef QUEUE_INTERNAL_H_
#define QUEUE_INTERNAL_H_

#include "uv-queue.h"

#include <assert.h>

/* This is a price for ABI stability.
 * We need one more pointer for certain QUEUEs
 * but can't add it because it would break the ABI.
 * So, we keep the required pointer as a first element of the QUEUE.
 */
struct uv__shared_queue_iterator_s {
  QUEUE queue;
  QUEUE* iter;
};

typedef struct uv__shared_queue_iterator_s uv__shared_queue_iterator_t;


static inline uv__shared_queue_iterator_t* uv__get_queue_iter(QUEUE* head) {
  QUEUE* first;
  uv__shared_queue_iterator_t* queue_iter;

  first = QUEUE_HEAD(head);
  assert(first != head);
  if (first == head) { /* for NDEBUG builds */
    return NULL;
  }

  queue_iter = QUEUE_DATA(first, uv__shared_queue_iterator_t, queue);
  return queue_iter;
}


int QUEUE_WITH_ITER_INIT(QUEUE* q);
void QUEUE_WITH_ITER_DESTROY(QUEUE* q);

static inline int QUEUE_WITH_ITER_EMPTY(const QUEUE* h) {
  const QUEUE* first = QUEUE_NEXT(h);
  assert(first != h); /* there must be an iterator */
  return h == QUEUE_NEXT(first);
}

static inline void QUEUE_WITH_ITER_INSERT_HEAD(QUEUE* h, QUEUE* q) {
  h = QUEUE_NEXT(h); /* skip iterator */
  QUEUE_INSERT_HEAD(h, q);
}

/* the same as QUEUE_INSERT_TAIL() but keep it for consistence */
static inline void QUEUE_WITH_ITER_INSERT_TAIL(QUEUE* h, QUEUE* q) {
  QUEUE_INSERT_TAIL(h, q);
}

static inline void QUEUE_WITH_ITER_REMOVE_SAFE(QUEUE* h, QUEUE* q) {
  uv__shared_queue_iterator_t* queue_iter;

  queue_iter = uv__get_queue_iter(h);
  assert(queue_iter != NULL);
  QUEUE_REMOVE_SAFE(q, &queue_iter->iter);
}

#define QUEUE_WITH_ITER_FOREACH_SAFE_BEGIN(q, h)                              \
  do {                                                                        \
    uv__shared_queue_iterator_t* queue_iter;                                  \
                                                                              \
    queue_iter = uv__get_queue_iter(h);                                       \
    assert(queue_iter != NULL);                                               \
                                                                              \
    for ((q) = QUEUE_NEXT(QUEUE_NEXT(h)), queue_iter->iter = QUEUE_NEXT(q);   \
         (q) != (h);                                                          \
         (q) = queue_iter->iter, queue_iter->iter = QUEUE_NEXT(queue_iter->iter))

#define QUEUE_WITH_ITER_FOREACH_SAFE_END() } while (0)

#endif /* QUEUE_INTERNAL_H_ */
