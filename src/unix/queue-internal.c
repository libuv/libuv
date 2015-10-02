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

#include "queue-internal.h"
#include "uv-common.h"


int QUEUE_WITH_ITER_INIT(QUEUE* head) {
  uv__shared_queue_iterator_t* queue_iter;

  queue_iter = uv__malloc(sizeof(*queue_iter));
  if (queue_iter == NULL) {
    return -ENOMEM;
  }

  QUEUE_INIT(head);

  QUEUE_INIT(&queue_iter->queue);
  queue_iter->iter = NULL;

  QUEUE_INSERT_HEAD(head, &queue_iter->queue);

  return 0;
}

void QUEUE_WITH_ITER_DESTROY(QUEUE* head) {
  QUEUE* first;
  uv__shared_queue_iterator_t* queue_iter;

  first = QUEUE_HEAD(head);
  assert(first != head);

  queue_iter = uv__get_queue_iter(head);
  assert(queue_iter != NULL);
  QUEUE_REMOVE(first);
  queue_iter->iter = NULL;
  uv__free(queue_iter);

  QUEUE_DESTROY(head);
}
