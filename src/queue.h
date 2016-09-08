/* Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
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

#ifndef QUEUE_H_
#define QUEUE_H_

#include <stddef.h>

#include "uv/queue.h"

/* Private macros. */
#define QUEUE_POISON_NEXT   (void*)0x101
#define QUEUE_POISON_PREV   (void*)0x102

static inline void QUEUE_POISON(QUEUE *q) {
  q->next = QUEUE_POISON_NEXT;
  q->prev = QUEUE_POISON_PREV;
}

/* Public macros. */
#define QUEUE_DATA(ptr, type, field)                                          \
  ((type *) ((char *) (ptr) - offsetof(type, field)))

/* Important note: mutating the list while QUEUE_FOREACH is
 * iterating over its elements results in undefined behavior.
 */
#define QUEUE_FOREACH(q, h)                                                   \
  for ((q) = (h)->next; (q) != (h); (q) = (q)->next)

/* Removing an element from the queue, while iterating over it,
 * should be done using QUEUE_REMOVE_SAFE() in conjunction with
 * QUEUE_FOREACH_SAFE() sharing the same n.
 * It's still not safe to remove the head though.
 */
#define QUEUE_FOREACH_SAFE(q, n, h)                                           \
  for ((q) = (h)->next, (n) = (q)->next;                                      \
       (q) != (h);                                                            \
       (q) = (n), (n) = (n)->next)

static inline int QUEUE_EMPTY(const QUEUE *q) {
  return q == q->next;
}

static inline QUEUE *QUEUE_HEAD(QUEUE *q) {
  return q->next;
}

static inline void QUEUE_INIT(QUEUE *q) {
  q->next = q;
  q->prev = q;
}

static inline void QUEUE_DESTROY(QUEUE *q) {
  QUEUE_POISON(q);
}

static inline void QUEUE_ADD(QUEUE *h, QUEUE *n) {
  h->prev->next = n->next;
  n->next->prev = h->prev;
  h->prev = n->prev;
  h->prev->next = h;
  QUEUE_POISON(n);
}

static inline void QUEUE_SPLIT(QUEUE *h, QUEUE *q, QUEUE *n) {
  n->prev = h->prev;
  n->prev->next = n;
  n->next = q;
  h->prev = q->prev;
  h->prev->next = h;
  q->prev = n;
}

static inline void QUEUE_MOVE(QUEUE *h, QUEUE *n) {
  if (QUEUE_EMPTY(h))
    QUEUE_INIT(n);
  else {
    QUEUE* q = QUEUE_HEAD(h);
    QUEUE_SPLIT(h, q, n);
  }
}

static inline void QUEUE_INSERT_HEAD(QUEUE *h, QUEUE *q) {
  q->next = h->next;
  q->prev = h;
  q->next->prev = q;
  h->next = q;
}

static inline void QUEUE_INSERT_TAIL(QUEUE *h, QUEUE *q) {
  q->next = h;
  q->prev = h->prev;
  q->prev->next = q;
  h->prev = q;
}

static inline void QUEUE_REMOVE(QUEUE *q) {
  q->prev->next = q->next;
  q->next->prev = q->prev;
  QUEUE_POISON(q);
}

/* Should be used to remove an element from the queue, while iterating over it,
 * in conjunction with QUEUE_FOREACH_SAFE() sharing the same n.
 * It's still not safe to remove the head though.
 */
static inline void QUEUE_REMOVE_SAFE(QUEUE *q, QUEUE **n) {
  if ((*n) == q) {
    *n = (*n)->next;
  }
  QUEUE_REMOVE(q);
}

#endif /* QUEUE_H_ */
