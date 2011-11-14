/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#include "uv.h"
#include "../uv-common.h"
#include "internal.h"
#include <assert.h>


#ifdef _MSC_VER /* msvc */
# define inline __inline
# define NOINLINE __declspec (noinline)
#else  /* gcc */
# define inline inline
# define NOINLINE __attribute__ ((noinline))
#endif


inline static int uv__rwlock_init_native(uv_rwlock_t* rwlock);
inline static void uv__rwlock_destroy_native(uv_rwlock_t* rwlock);
inline static void uv__rwlock_rdlock_native(uv_rwlock_t* rwlock);
inline static int uv__rwlock_tryrdlock_native(uv_rwlock_t* rwlock);
inline static void uv__rwlock_rdunlock_native(uv_rwlock_t* rwlock);
inline static void uv__rwlock_wrlock_native(uv_rwlock_t* rwlock);
inline static int uv__rwlock_trywrlock_native(uv_rwlock_t* rwlock);
inline static void uv__rwlock_wrunlock_native(uv_rwlock_t* rwlock);

inline static int uv__rwlock_init_fallback(uv_rwlock_t* rwlock);
inline static void uv__rwlock_destroy_fallback(uv_rwlock_t* rwlock);
inline static void uv__rwlock_rdlock_fallback(uv_rwlock_t* rwlock);
inline static int uv__rwlock_tryrdlock_fallback(uv_rwlock_t* rwlock);
inline static void uv__rwlock_rdunlock_fallback(uv_rwlock_t* rwlock);
inline static void uv__rwlock_wrlock_fallback(uv_rwlock_t* rwlock);
inline static int uv__rwlock_trywrlock_fallback(uv_rwlock_t* rwlock);
inline static void uv__rwlock_wrunlock_fallback(uv_rwlock_t* rwlock);


static NOINLINE void uv__once_inner(uv_once_t* guard,
    void (*callback)(void)) {
  DWORD result;
  HANDLE existing_event, created_event;
  HANDLE* event_ptr;

  /* Fetch and align event_ptr */
  event_ptr = (HANDLE*) (((uintptr_t) &guard->event + (sizeof(HANDLE) - 1)) &
    ~(sizeof(HANDLE) - 1));

  created_event = CreateEvent(NULL, 1, 0, NULL);
  if (created_event == 0) {
    /* Could fail in a low-memory situation? */
    uv_fatal_error(GetLastError(), "CreateEvent");
  }

  existing_event = InterlockedCompareExchangePointer(event_ptr,
                                                     created_event,
                                                     NULL);

  if (existing_event == NULL) {
    /* We won the race */
    callback();

    result = SetEvent(created_event);
    assert(result);
    guard->ran = 1;

  } else {
    /* We lost the race. Destroy the event we created and wait for the */
    /* existing one to become signaled. */
    CloseHandle(created_event);
    result = WaitForSingleObject(existing_event, INFINITE);
    assert(result == WAIT_OBJECT_0);
  }
}


void uv_once(uv_once_t* guard, void (*callback)(void)) {
  /* Fast case - avoid WaitForSingleObject. */
  if (guard->ran) {
    return;
  }

  uv__once_inner(guard, callback);
}

int uv_mutex_init(uv_mutex_t* mutex) {
  InitializeCriticalSection(mutex);
  return 0;
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  DeleteCriticalSection(mutex);
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  EnterCriticalSection(mutex);
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  if (TryEnterCriticalSection(mutex))
    return 0;
  else
    return -1;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  LeaveCriticalSection(mutex);
}


int uv_rwlock_init(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    return uv__rwlock_init_native(rwlock);
  else
    return uv__rwlock_init_fallback(rwlock);
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    uv__rwlock_destroy_native(rwlock);
  else
    uv__rwlock_destroy_fallback(rwlock);
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    uv__rwlock_rdlock_native(rwlock);
  else
    uv__rwlock_rdlock_fallback(rwlock);
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    return uv__rwlock_tryrdlock_native(rwlock);
  else
    return uv__rwlock_tryrdlock_fallback(rwlock);
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    uv__rwlock_rdunlock_native(rwlock);
  else
    uv__rwlock_rdunlock_fallback(rwlock);
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    uv__rwlock_wrlock_native(rwlock);
  else
    uv__rwlock_wrlock_fallback(rwlock);
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    return uv__rwlock_trywrlock_native(rwlock);
  else
    return uv__rwlock_trywrlock_fallback(rwlock);
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared)
    uv__rwlock_wrunlock_native(rwlock);
  else
    uv__rwlock_wrunlock_fallback(rwlock);
}


inline static int uv__rwlock_init_native(uv_rwlock_t* rwlock) {
  pInitializeSRWLock(&rwlock->srwlock_);
  return 0;
}


inline static void uv__rwlock_destroy_native(uv_rwlock_t* rwlock) {
  (void) rwlock;
}


inline static void uv__rwlock_rdlock_native(uv_rwlock_t* rwlock) {
  pAcquireSRWLockShared(&rwlock->srwlock_);
}


inline static int uv__rwlock_tryrdlock_native(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared(&rwlock->srwlock_))
    return 0;
  else
    return -1;
}


inline static void uv__rwlock_rdunlock_native(uv_rwlock_t* rwlock) {
  pReleaseSRWLockShared(&rwlock->srwlock_);
}


inline static void uv__rwlock_wrlock_native(uv_rwlock_t* rwlock) {
  pAcquireSRWLockExclusive(&rwlock->srwlock_);
}


inline static int uv__rwlock_trywrlock_native(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockExclusive(&rwlock->srwlock_))
    return 0;
  else
    return -1;
}


inline static void uv__rwlock_wrunlock_native(uv_rwlock_t* rwlock) {
  pReleaseSRWLockExclusive(&rwlock->srwlock_);
}


inline static int uv__rwlock_init_fallback(uv_rwlock_t* rwlock) {
  if (uv_mutex_init(&rwlock->fallback_.read_mutex_))
    return -1;

  if (uv_mutex_init(&rwlock->fallback_.write_mutex_)) {
    uv_mutex_destroy(&rwlock->fallback_.read_mutex_);
    return -1;
  }

  rwlock->fallback_.num_readers_ = 0;

  return 0;
}


inline static void uv__rwlock_destroy_fallback(uv_rwlock_t* rwlock) {
  uv_mutex_destroy(&rwlock->fallback_.read_mutex_);
  uv_mutex_destroy(&rwlock->fallback_.write_mutex_);
}


inline static void uv__rwlock_rdlock_fallback(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.read_mutex_);

  if (++rwlock->fallback_.num_readers_ == 1)
    uv_mutex_lock(&rwlock->fallback_.write_mutex_);

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);
}


inline static int uv__rwlock_tryrdlock_fallback(uv_rwlock_t* rwlock) {
  int ret;

  ret = -1;

  if (uv_mutex_trylock(&rwlock->fallback_.read_mutex_))
    goto out;

  if (rwlock->fallback_.num_readers_ == 0)
    ret = uv_mutex_trylock(&rwlock->fallback_.write_mutex_);
  else
    ret = 0;

  if (ret == 0)
    rwlock->fallback_.num_readers_++;

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);

out:
  return ret;
}


inline static void uv__rwlock_rdunlock_fallback(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.read_mutex_);

  if (--rwlock->fallback_.num_readers_ == 0)
    uv_mutex_unlock(&rwlock->fallback_.write_mutex_);

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);
}


inline static void uv__rwlock_wrlock_fallback(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.write_mutex_);
}


inline static int uv__rwlock_trywrlock_fallback(uv_rwlock_t* rwlock) {
  return uv_mutex_trylock(&rwlock->fallback_.write_mutex_);
}


inline static void uv__rwlock_wrunlock_fallback(uv_rwlock_t* rwlock) {
  uv_mutex_unlock(&rwlock->fallback_.write_mutex_);
}
