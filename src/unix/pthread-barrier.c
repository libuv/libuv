/*
Copyright (c) 2016, Kari Tristan Helgason <kthelgason@gmail.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "pthread-barrier.h"

#include <stdlib.h>
#include <assert.h>

/* TODO: support barrier_attr */
int pthread_barrier_init(pthread_barrier_t* barrier,
                         const void* barrier_attr,
                         unsigned count) {
  int rc;

  if (barrier == NULL || count == 0)
    return EINVAL;

  if (barrier_attr != NULL)
    return ENOTSUP;

  barrier->in = 0;
  barrier->out = 0;
  barrier->threshold = count;

  if ((rc = pthread_mutex_init(&barrier->mutex, NULL)) != 0)
    return rc;
  if ((rc = pthread_cond_init(&barrier->cond, NULL)) != 0)
    pthread_mutex_destroy(&barrier->mutex);

  return rc;
}

int pthread_barrier_wait(pthread_barrier_t* barrier) {
  int rc;

  if (barrier == NULL)
    return EINVAL;

  /* Lock the mutex*/
  if ((rc = pthread_mutex_lock(&barrier->mutex)) != 0)
    return rc;

  /* Increment the count. If this is the first thread to reach the threshold,
     wake up waiters, unlock the mutex, then return
     PTHREAD_BARRIER_SERIAL_THREAD. */
  if (++barrier->in == barrier->threshold) {
    barrier->in = 0;
    barrier->out = barrier->threshold - 1;
    rc = pthread_cond_signal(&barrier->cond);
    assert(rc == 0);

    pthread_mutex_unlock(&barrier->mutex);
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  /* Otherwise, wait for other threads until in is set to 0,
     then return 0 to indicate this is not the first thread. */
  do {
    if ((rc = pthread_cond_wait(&barrier->cond, &barrier->mutex)) != 0)
      break;
  } while (barrier->in != 0);

  /* mark thread exit */
  barrier->out--;
  pthread_cond_signal(&barrier->cond);
  pthread_mutex_unlock(&barrier->mutex);
  return rc;
}

int pthread_barrier_destroy(pthread_barrier_t* barrier) {
  int rc;

  if (barrier == NULL)
    return EINVAL;

  if ((rc = pthread_mutex_lock(&barrier->mutex)) != 0)
    return rc;

  if (barrier->in > 0 || barrier->out > 0)
    rc = EBUSY;

  pthread_mutex_unlock(&barrier->mutex);

  if (rc)
    return rc;

  pthread_cond_destroy(&barrier->cond);
  pthread_mutex_destroy(&barrier->mutex);
  return 0;
}
