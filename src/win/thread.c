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

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#if defined(__MINGW64_VERSION_MAJOR)
/* MemoryBarrier expands to __mm_mfence in some cases (x86+sse2), which may
 * require this header in some versions of mingw64. */
#include <intrin.h>
#endif

#include "uv.h"
#include "internal.h"

static void uv__once_inner(uv_once_t* guard, void (*callback)(void)) {
  DWORD result;
  HANDLE existing_event, created_event;

  created_event = CreateEvent(NULL, 1, 0, NULL);
  if (created_event == 0) {
    /* Could fail in a low-memory situation? */
    uv_fatal_error(GetLastError(), "CreateEvent");
  }

  existing_event = InterlockedCompareExchangePointer(&guard->event,
                                                     created_event,
                                                     NULL);

  if (existing_event == NULL) {
    /* We won the race */
    callback();

    result = SetEvent(created_event);
    assert(result);
    guard->ran = 1;

  } else {
    /* We lost the race. Destroy the event we created and wait for the existing
     * one to become signaled. */
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


/* Verify that uv_thread_t can be stored in a TLS slot. */
STATIC_ASSERT(sizeof(uv_thread_t) <= sizeof(void*));

static uv_key_t uv__current_thread_key;
static uv_once_t uv__current_thread_init_guard = UV_ONCE_INIT;


static void uv__init_current_thread_key(void) {
  if (uv_key_create(&uv__current_thread_key))
    abort();
}


struct thread_ctx {
  void (*entry)(void* arg);
  void* arg;
  uv_thread_t self;
};


/* Win32 processor group management. This code will have _no_ effect except on a system with >64 HW
   threads or more than one CPU socket.  On those systems, threads created will be assigned
   sequentially to processor groups as the processor groups fill. So there wil be no effect until
   the 64th thread is created on a system witth 128 HW threads (such as an AMD 64-core threadwripper). */

#define MAX_PROCESSOR_GROUPS 8
static int uv_num_win32_processor_groups = 1;
static int uv_num_threads_per_processor_group[MAX_PROCESSOR_GROUPS];
static uv_once_t uv_query_processor_groups_guard = UV_ONCE_INIT;
static int uv_total_num_hwthreads = 0;						/* not used unless uv_num_win32_processor_groups > 1 */
static volatile LONG uv_win32_thread_creation_count = 0;
static HMODULE uv_win32_kernel32_dll_handle;

typedef WORD(WINAPI *fnGetActiveProcessorGroupCount)(void);
typedef BOOL(WINAPI *fnSetThreadGroupAffinity)(HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
typedef DWORD(WINAPI *fnGetMaximumProcessorCount)(DWORD);
typedef BOOL(WINAPI *fnGetThreadGroupAffinity)(HANDLE, PGROUP_AFFINITY);

static fnGetActiveProcessorGroupCount uv_win32_GetActiveProcessorGroupCount = NULL;
static fnSetThreadGroupAffinity uv_win32_SetThreadGroupAffinity = NULL;
static fnGetMaximumProcessorCount uv_win32_GetMaximumProcessorCount = NULL;
static fnGetThreadGroupAffinity uv_win32_GetThreadGroupAffinity = NULL;



static UINT __stdcall uv__thread_start(void* arg) {
  struct thread_ctx *ctx_p;
  struct thread_ctx ctx;

  ctx_p = arg;
  ctx = *ctx_p;
  uv__free(ctx_p);

  uv_once(&uv__current_thread_init_guard, uv__init_current_thread_key);
  uv_key_set(&uv__current_thread_key, ctx.self);

  /* now, if there is more than one processor group, figure out which to assign it to. Note that this code will not execute on any system on which libuv does not 
	 suffer from the win32 threadgroup problem */
  if ( uv_num_win32_processor_groups > 1 )
  {
	  LONG our_thread_index = InterlockedIncrement( &uv_win32_thread_creation_count );
	  our_thread_index %= uv_total_num_hwthreads;			/* wrap around when we create more threads than their are HW threads */
	  WORD processor_group_to_use = 0;
	  while( our_thread_index > 0 )
	  {
		  our_thread_index -= uv_num_threads_per_processor_group[processor_group_to_use];
		  if ( our_thread_index > 0 )
		  {
			  processor_group_to_use++;
		  }
	  }
	  HANDLE our_thread_handle = GetCurrentThread();
	  GROUP_AFFINITY old_affinity;
	  if ( uv_win32_GetThreadGroupAffinity( our_thread_handle, &old_affinity ) )
	  {
		  old_affinity.Group = processor_group_to_use;
		  uv_win32_SetThreadGroupAffinity( our_thread_handle, &old_affinity, NULL );
	  }
  }
  
  ctx.entry(ctx.arg);

  return 0;
}


static void uv_init_processor_group_info( void ) {
	/* Because not all  versions of win32 include the processor group entry points in kernel32, we will look for them in the dll instead of callig directly */
	uv_win32_kernel32_dll_handle = LoadLibraryA( "kernel32" );
	if ( ! uv_win32_kernel32_dll_handle )
	{
		/* failed to open kernel32? how are we even running? */
		return;
	}

	/* find the functions in the dll. if not found, this is a very old windows machine that doesn't support these kinds of cpus */
	uv_win32_GetActiveProcessorGroupCount = ( fnGetActiveProcessorGroupCount )
		GetProcAddress( uv_win32_kernel32_dll_handle, "GetActiveProcessorGroupCount" );
	uv_win32_SetThreadGroupAffinity = ( fnSetThreadGroupAffinity )
		GetProcAddress( uv_win32_kernel32_dll_handle, "SetThreadGroupAffinity" );
	uv_win32_GetMaximumProcessorCount = ( fnGetMaximumProcessorCount )
		GetProcAddress( uv_win32_kernel32_dll_handle, "GetMaximumProcessorCount" );
	uv_win32_GetThreadGroupAffinity = ( fnGetThreadGroupAffinity )
		GetProcAddress( uv_win32_kernel32_dll_handle, "GetThreadGroupAffinity" );
		
	if ( uv_win32_SetThreadGroupAffinity && uv_win32_GetActiveProcessorGroupCount &&
		 uv_win32_GetMaximumProcessorCount && uv_win32_GetThreadGroupAffinity )
	{
		uv_num_win32_processor_groups = uv_win32_GetActiveProcessorGroupCount();
		if ( uv_num_win32_processor_groups > MAX_PROCESSOR_GROUPS )
		{
			uv_num_win32_processor_groups = MAX_PROCESSOR_GROUPS;
		}
		if ( uv_num_win32_processor_groups > 1 )
		{
			WORD grp;
			for( grp = 0; grp < uv_num_win32_processor_groups; grp++ )
			{
				uv_num_threads_per_processor_group[grp] = uv_win32_GetMaximumProcessorCount( grp );
				uv_total_num_hwthreads += uv_num_threads_per_processor_group[grp];
			}
		}
	}
}

int uv_thread_create(uv_thread_t *tid, void (*entry)(void *arg), void *arg) {
  uv_thread_options_t params;
  params.flags = UV_THREAD_NO_FLAGS;
  return uv_thread_create_ex(tid, &params, entry, arg);
}

int uv_thread_create_ex(uv_thread_t* tid,
                        const uv_thread_options_t* params,
                        void (*entry)(void *arg),
                        void *arg) {
  struct thread_ctx* ctx;
  int err;
  HANDLE thread;
  SYSTEM_INFO sysinfo;
  size_t stack_size;
  size_t pagesize;

  /* Query processor groups the first time */
  uv_once( &uv_query_processor_groups_guard, uv_init_processor_group_info );

  stack_size =
      params->flags & UV_THREAD_HAS_STACK_SIZE ? params->stack_size : 0;

  if (stack_size != 0) {
    GetNativeSystemInfo(&sysinfo);
    pagesize = (size_t)sysinfo.dwPageSize;
    /* Round up to the nearest page boundary. */
    stack_size = (stack_size + pagesize - 1) &~ (pagesize - 1);

    if ((unsigned)stack_size != stack_size)
      return UV_EINVAL;
  }

  ctx = uv__malloc(sizeof(*ctx));
  if (ctx == NULL)
    return UV_ENOMEM;

  ctx->entry = entry;
  ctx->arg = arg;

  /* Create the thread in suspended state so we have a chance to pass
   * its own creation handle to it */
  thread = (HANDLE) _beginthreadex(NULL,
                                   (unsigned)stack_size,
                                   uv__thread_start,
                                   ctx,
                                   CREATE_SUSPENDED,
                                   NULL);
  if (thread == NULL) {
    err = errno;
    uv__free(ctx);
  } else {
    err = 0;
    *tid = thread;
    ctx->self = thread;
    ResumeThread(thread);
  }

  switch (err) {
    case 0:
      return 0;
    case EACCES:
      return UV_EACCES;
    case EAGAIN:
      return UV_EAGAIN;
    case EINVAL:
      return UV_EINVAL;
  }

  return UV_EIO;
}


uv_thread_t uv_thread_self(void) {
  uv_thread_t key;
  uv_once(&uv__current_thread_init_guard, uv__init_current_thread_key);
  key = uv_key_get(&uv__current_thread_key);
  if (key == NULL) {
      /* If the thread wasn't started by uv_thread_create (such as the main
       * thread), we assign an id to it now. */
      if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                           GetCurrentProcess(), &key, 0,
                           FALSE, DUPLICATE_SAME_ACCESS)) {
          uv_fatal_error(GetLastError(), "DuplicateHandle");
      }
      uv_key_set(&uv__current_thread_key, key);
  }
  return key;
}


int uv_thread_join(uv_thread_t *tid) {
  if (WaitForSingleObject(*tid, INFINITE))
    return uv_translate_sys_error(GetLastError());
  else {
    CloseHandle(*tid);
    *tid = 0;
    MemoryBarrier();  /* For feature parity with pthread_join(). */
    return 0;
  }
}


int uv_thread_equal(const uv_thread_t* t1, const uv_thread_t* t2) {
  return *t1 == *t2;
}


int uv_mutex_init(uv_mutex_t* mutex) {
  InitializeCriticalSection(mutex);
  return 0;
}


int uv_mutex_init_recursive(uv_mutex_t* mutex) {
  return uv_mutex_init(mutex);
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
    return UV_EBUSY;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  LeaveCriticalSection(mutex);
}

/* Ensure that the ABI for this type remains stable in v1.x */
#ifdef _WIN64
STATIC_ASSERT(sizeof(uv_rwlock_t) == 80);
#else
STATIC_ASSERT(sizeof(uv_rwlock_t) == 48);
#endif

int uv_rwlock_init(uv_rwlock_t* rwlock) {
  memset(rwlock, 0, sizeof(*rwlock));
  InitializeSRWLock(&rwlock->read_write_lock_);

  return 0;
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  /* SRWLock does not need explicit destruction so long as there are no waiting threads
     See: https://docs.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializesrwlock#remarks */
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  AcquireSRWLockShared(&rwlock->read_write_lock_);
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  if (!TryAcquireSRWLockShared(&rwlock->read_write_lock_))
    return UV_EBUSY;

  return 0;
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  ReleaseSRWLockShared(&rwlock->read_write_lock_);
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  AcquireSRWLockExclusive(&rwlock->read_write_lock_);
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  if (!TryAcquireSRWLockExclusive(&rwlock->read_write_lock_))
    return UV_EBUSY;

  return 0;
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  ReleaseSRWLockExclusive(&rwlock->read_write_lock_);
}


int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  *sem = CreateSemaphore(NULL, value, INT_MAX, NULL);
  if (*sem == NULL)
    return uv_translate_sys_error(GetLastError());
  else
    return 0;
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (!CloseHandle(*sem))
    abort();
}


void uv_sem_post(uv_sem_t* sem) {
  if (!ReleaseSemaphore(*sem, 1, NULL))
    abort();
}


void uv_sem_wait(uv_sem_t* sem) {
  if (WaitForSingleObject(*sem, INFINITE) != WAIT_OBJECT_0)
    abort();
}


int uv_sem_trywait(uv_sem_t* sem) {
  DWORD r = WaitForSingleObject(*sem, 0);

  if (r == WAIT_OBJECT_0)
    return 0;

  if (r == WAIT_TIMEOUT)
    return UV_EAGAIN;

  abort();
  return -1; /* Satisfy the compiler. */
}


int uv_cond_init(uv_cond_t* cond) {
  InitializeConditionVariable(&cond->cond_var);
  return 0;
}


void uv_cond_destroy(uv_cond_t* cond) {
  /* nothing to do */
  (void) &cond;
}


void uv_cond_signal(uv_cond_t* cond) {
  WakeConditionVariable(&cond->cond_var);
}


void uv_cond_broadcast(uv_cond_t* cond) {
  WakeAllConditionVariable(&cond->cond_var);
}


void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  if (!SleepConditionVariableCS(&cond->cond_var, mutex, INFINITE))
    abort();
}

int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  if (SleepConditionVariableCS(&cond->cond_var, mutex, (DWORD)(timeout / 1e6)))
    return 0;
  if (GetLastError() != ERROR_TIMEOUT)
    abort();
  return UV_ETIMEDOUT;
}


int uv_barrier_init(uv_barrier_t* barrier, unsigned int count) {
  int err;

  barrier->n = count;
  barrier->count = 0;

  err = uv_mutex_init(&barrier->mutex);
  if (err)
    return err;

  err = uv_sem_init(&barrier->turnstile1, 0);
  if (err)
    goto error2;

  err = uv_sem_init(&barrier->turnstile2, 1);
  if (err)
    goto error;

  return 0;

error:
  uv_sem_destroy(&barrier->turnstile1);
error2:
  uv_mutex_destroy(&barrier->mutex);
  return err;

}


void uv_barrier_destroy(uv_barrier_t* barrier) {
  uv_sem_destroy(&barrier->turnstile2);
  uv_sem_destroy(&barrier->turnstile1);
  uv_mutex_destroy(&barrier->mutex);
}


int uv_barrier_wait(uv_barrier_t* barrier) {
  int serial_thread;

  uv_mutex_lock(&barrier->mutex);
  if (++barrier->count == barrier->n) {
    uv_sem_wait(&barrier->turnstile2);
    uv_sem_post(&barrier->turnstile1);
  }
  uv_mutex_unlock(&barrier->mutex);

  uv_sem_wait(&barrier->turnstile1);
  uv_sem_post(&barrier->turnstile1);

  uv_mutex_lock(&barrier->mutex);
  serial_thread = (--barrier->count == 0);
  if (serial_thread) {
    uv_sem_wait(&barrier->turnstile1);
    uv_sem_post(&barrier->turnstile2);
  }
  uv_mutex_unlock(&barrier->mutex);

  uv_sem_wait(&barrier->turnstile2);
  uv_sem_post(&barrier->turnstile2);
  return serial_thread;
}


int uv_key_create(uv_key_t* key) {
  key->tls_index = TlsAlloc();
  if (key->tls_index == TLS_OUT_OF_INDEXES)
    return UV_ENOMEM;
  return 0;
}


void uv_key_delete(uv_key_t* key) {
  if (TlsFree(key->tls_index) == FALSE)
    abort();
  key->tls_index = TLS_OUT_OF_INDEXES;
}


void* uv_key_get(uv_key_t* key) {
  void* value;

  value = TlsGetValue(key->tls_index);
  if (value == NULL)
    if (GetLastError() != ERROR_SUCCESS)
      abort();

  return value;
}


void uv_key_set(uv_key_t* key, void* value) {
  if (TlsSetValue(key->tls_index, value) == FALSE)
    abort();
}
