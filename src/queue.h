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

/* Public macros. */
#define QUEUE_DATA(ptr, type, field)                                          \
  ((type *) ((char *) (ptr) - offsetof(type, field)))

/* Important note: mutating the list while QUEUE_FOREACH is
 * iterating over its elements results in undefined behavior.
 */
#define QUEUE_FOREACH(q, h)                                                   \
  for ((q) = (h)->next; (q) != (h); (q) = (q)->next)

#define QUEUE_EMPTY(q)                                                        \
  ((q) == (q)->next)

#define QUEUE_HEAD(q)                                                         \
  ((q)->next)

#define QUEUE_INIT(q)                                                         \
  do {                                                                        \
    (q)->next = (q);                                                          \
    (q)->prev = (q);                                                          \
  }                                                                           \
  while (0)

#define QUEUE_ADD(h, n)                                                       \
  do {                                                                        \
    (h)->prev->next = (n)->next;                                              \
    (n)->next->prev = (h)->prev;                                              \
    (h)->prev = (n)->prev;                                                    \
    (h)->prev->next = (h);                                                    \
  }                                                                           \
  while (0)

#define QUEUE_SPLIT(h, q, n)                                                  \
  do {                                                                        \
    (n)->prev = (h)->prev;                                                    \
    (n)->prev->next = (n);                                                    \
    (n)->next = (q);                                                          \
    (h)->prev = (q)->prev;                                                    \
    (h)->prev->next = (h);                                                    \
    (q)->prev = (n);                                                          \
  }                                                                           \
  while (0)

#define QUEUE_MOVE(h, n)                                                      \
  do {                                                                        \
    if (QUEUE_EMPTY(h))                                                       \
      QUEUE_INIT(n);                                                          \
    else {                                                                    \
      QUEUE* q = QUEUE_HEAD(h);                                               \
      QUEUE_SPLIT(h, q, n);                                                   \
    }                                                                         \
  }                                                                           \
  while (0)

#define QUEUE_INSERT_HEAD(h, q)                                               \
  do {                                                                        \
    (q)->next = (h)->next;                                                    \
    (q)->prev = (h);                                                          \
    (q)->next->prev = (q);                                                    \
    (h)->next = (q);                                                          \
  }                                                                           \
  while (0)

#define QUEUE_INSERT_TAIL(h, q)                                               \
  do {                                                                        \
    (q)->next = (h);                                                          \
    (q)->prev = (h)->prev;                                                    \
    (q)->prev->next = (q);                                                    \
    (h)->prev = (q);                                                          \
  }                                                                           \
  while (0)

#define QUEUE_REMOVE(q)                                                       \
  do {                                                                        \
    (q)->prev->next = (q)->next;                                              \
    (q)->next->prev = (q)->prev;                                              \
  }                                                                           \
  while (0)

#endif /* QUEUE_H_ */
