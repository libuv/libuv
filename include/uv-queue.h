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

#ifndef UV_QUEUE_H_
#define UV_QUEUE_H_

#include <stddef.h>

struct QUEUE { struct QUEUE *next, *prev; };
typedef struct QUEUE QUEUE;

/* Private macros. */
#define QUEUE_NEXT(q)       ((q)->next)
#define QUEUE_PREV(q)       ((q)->prev)
#define QUEUE_PREV_NEXT(q)  (QUEUE_NEXT(QUEUE_PREV(q)))
#define QUEUE_NEXT_PREV(q)  (QUEUE_PREV(QUEUE_NEXT(q)))

/* Public macros. */
#define QUEUE_DATA(ptr, type, field)                                          \
  ((type *) ((char *) (ptr) - offsetof(type, field)))

/* Important note: mutating the list while QUEUE_FOREACH is
 * iterating over its elements results in undefined behavior.
 */
#define QUEUE_FOREACH(q, h)                                                   \
  for ((q) = QUEUE_NEXT(h); (q) != (h); (q) = QUEUE_NEXT(q))

/* Removing an element from the queue, while iterating over it,
 * should be done using QUEUE_REMOVE_SAFE() in conjunction with
 * QUEUE_FOREACH_SAFE() sharing the same n.
 * It's still not safe to delete the head though.
 */
#define QUEUE_FOREACH_SAFE(q, n, h)                                           \
  for ((q) = QUEUE_NEXT(h), (n) = QUEUE_NEXT(q);                              \
       (q) != (h);                                                            \
       (q) = (n), (n) = QUEUE_NEXT(n))

static inline int QUEUE_EMPTY(const QUEUE *q) {
  return q == QUEUE_NEXT(q);
}

static inline QUEUE * QUEUE_HEAD(QUEUE *q) {
  return QUEUE_NEXT(q);
}

static inline void QUEUE_INIT(QUEUE *q) {
  QUEUE_NEXT(q) = q;
  QUEUE_PREV(q) = q;
}

static inline void QUEUE_ADD(QUEUE *h, QUEUE *n) {
  QUEUE_PREV_NEXT(h) = QUEUE_NEXT(n);
  QUEUE_NEXT_PREV(n) = QUEUE_PREV(h);
  QUEUE_PREV(h) = QUEUE_PREV(n);
  QUEUE_PREV_NEXT(h) = (h);
}

static inline void QUEUE_SPLIT(QUEUE *h, QUEUE *q, QUEUE *n) {
  QUEUE_PREV(n) = QUEUE_PREV(h);
  QUEUE_PREV_NEXT(n) = n;
  QUEUE_NEXT(n) = q;
  QUEUE_PREV(h) = QUEUE_PREV(q);
  QUEUE_PREV_NEXT(h) = h;
  QUEUE_PREV(q) = n;
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
  QUEUE_NEXT(q) = QUEUE_NEXT(h);
  QUEUE_PREV(q) = h;
  QUEUE_NEXT_PREV(q) = q;
  QUEUE_NEXT(h) = q;
}

static inline void QUEUE_INSERT_TAIL(QUEUE *h, QUEUE *q) {
  QUEUE_NEXT(q) = h;
  QUEUE_PREV(q) = QUEUE_PREV(h);
  QUEUE_PREV_NEXT(q) = q;
  QUEUE_PREV(h) = q;
}

static inline void QUEUE_REMOVE(QUEUE *q) {
  QUEUE_PREV_NEXT(q) = QUEUE_NEXT(q);
  QUEUE_NEXT_PREV(q) = QUEUE_PREV(q);
}

/* Should be used to remove an element from the queue, while iterating over it,
 * in conjunction with QUEUE_FOREACH_SAFE() sharing the same n.
 * It's still not safe to delete the head though.
 */
static inline void QUEUE_REMOVE_SAFE(QUEUE *q, QUEUE **n) {
  if ((*n) == q) {
    *n = QUEUE_NEXT(*n);
  }
  QUEUE_REMOVE(q);
}

#endif /* UV_QUEUE_H_ */
