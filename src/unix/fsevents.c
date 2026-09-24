/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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
#include "internal.h"

#if TARGET_OS_IPHONE || MAC_OS_X_VERSION_MAX_ALLOWED < 1070

/* iOS (currently) doesn't provide the FSEvents-API (nor CoreServices) */
/* macOS prior to 10.7 doesn't provide the full FSEvents API so use kqueue */

int uv__fsevents_init(uv_fs_event_t* handle) {
  return 0;
}


int uv__fsevents_close(uv_fs_event_t* handle) {
  return 0;
}


void uv__fsevents_loop_delete(uv_loop_t* loop) {
}

#else /* TARGET_OS_IPHONE */

#include "darwin-stub.h"

#include <dlfcn.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>

static const int kFSEventsModified =
    kFSEventStreamEventFlagItemChangeOwner |
    kFSEventStreamEventFlagItemFinderInfoMod |
    kFSEventStreamEventFlagItemInodeMetaMod |
    kFSEventStreamEventFlagItemModified |
    kFSEventStreamEventFlagItemXattrMod;

static const int kFSEventsRenamed =
    kFSEventStreamEventFlagItemCreated |
    kFSEventStreamEventFlagItemRemoved |
    kFSEventStreamEventFlagItemRenamed;

static const int kFSEventsSystem =
    kFSEventStreamEventFlagUserDropped |
    kFSEventStreamEventFlagKernelDropped |
    kFSEventStreamEventFlagEventIdsWrapped |
    kFSEventStreamEventFlagHistoryDone |
    kFSEventStreamEventFlagMount |
    kFSEventStreamEventFlagUnmount |
    kFSEventStreamEventFlagRootChanged;

typedef struct uv__fsevents_event_s uv__fsevents_event_t;

/* Keep the public uv_fs_event_t layout unchanged. cf_cb points to async. */
typedef struct {
  uv_async_t async;
  FSEventStreamEventId event_id;
  uint64_t generation;
  FSEventStreamRef live_stream;
  int ready;
  int live_only;
} uv__fsevents_handle_t;
typedef struct uv__cf_loop_signal_s uv__cf_loop_signal_t;
typedef struct uv__cf_loop_state_s uv__cf_loop_state_t;

enum uv__cf_loop_signal_type_e {
  kUVCFLoopSignalRegular,
  kUVCFLoopSignalClosing
};
typedef enum uv__cf_loop_signal_type_e uv__cf_loop_signal_type_t;

struct uv__cf_loop_signal_s {
  struct uv__queue member;
  uv_fs_event_t* handle;
  uv__cf_loop_signal_type_t type;
};

struct uv__fsevents_event_s {
  struct uv__queue member;
  int events;
  char path[1];
};

struct uv__cf_loop_state_s {
  CFRunLoopRef loop;
  CFRunLoopSourceRef signal_source;
  int fsevent_need_reschedule;
  FSEventStreamRef fsevent_stream;
  uv_cond_t fsevent_cond;
  uv_mutex_t fsevent_mutex;
  struct uv__queue fsevent_handles;
  unsigned int fsevent_handle_count;
  uint64_t fsevent_generation;
  uint64_t fsevent_stream_generation;
};

/* Forward declarations */
static void uv__cf_loop_cb(void* arg);
static void* uv__cf_loop_runner(void* arg);
static int uv__cf_loop_signal(uv_loop_t* loop, uv_fs_event_t* handle);

/* Lazy-loaded by uv__fsevents_global_init(). */
static CFArrayRef (*pCFArrayCreate)(CFAllocatorRef,
                                    const void**,
                                    CFIndex,
                                    const CFArrayCallBacks*);
static void (*pCFRelease)(CFTypeRef);
static void (*pCFRunLoopAddSource)(CFRunLoopRef,
                                   CFRunLoopSourceRef,
                                   CFStringRef);
static CFRunLoopRef (*pCFRunLoopGetCurrent)(void);
static void (*pCFRunLoopRemoveSource)(CFRunLoopRef,
                                      CFRunLoopSourceRef,
                                      CFStringRef);
static void (*pCFRunLoopRun)(void);
static CFRunLoopSourceRef (*pCFRunLoopSourceCreate)(CFAllocatorRef,
                                                    CFIndex,
                                                    CFRunLoopSourceContext*);
static void (*pCFRunLoopSourceSignal)(CFRunLoopSourceRef);
static void (*pCFRunLoopStop)(CFRunLoopRef);
static void (*pCFRunLoopWakeUp)(CFRunLoopRef);
static CFStringRef (*pCFStringCreateWithFileSystemRepresentation)(
    CFAllocatorRef,
    const char*);
static CFStringRef (*pkCFRunLoopDefaultMode);
static FSEventStreamRef (*pFSEventStreamCreate)(CFAllocatorRef,
                                                FSEventStreamCallback,
                                                FSEventStreamContext*,
                                                CFArrayRef,
                                                FSEventStreamEventId,
                                                CFTimeInterval,
                                                FSEventStreamCreateFlags);
static void (*pFSEventStreamInvalidate)(FSEventStreamRef);
static void (*pFSEventStreamRelease)(FSEventStreamRef);
static void (*pFSEventStreamScheduleWithRunLoop)(FSEventStreamRef,
                                                 CFRunLoopRef,
                                                 CFStringRef);
static int (*pFSEventStreamStart)(FSEventStreamRef);
static void (*pFSEventStreamStop)(FSEventStreamRef);
static FSEventStreamEventId (*pFSEventsGetCurrentEventId)(void);
static CFTypeRef (*pFSEventsCopyUUIDForDevice)(dev_t);

#define UV__FSEVENTS_PROCESS(handle, block)                                   \
    do {                                                                      \
      struct uv__queue events;                                                \
      struct uv__queue* q;                                                    \
      uv__fsevents_event_t* event;                                            \
      int err;                                                                \
      uv_mutex_lock(&(handle)->cf_mutex);                                     \
      /* Split-off all events and empty original queue */                     \
      uv__queue_move(&(handle)->cf_events, &events);                          \
      /* Get error (if any) and zero original one */                          \
      err = (handle)->cf_error;                                               \
      (handle)->cf_error = 0;                                                 \
      uv_mutex_unlock(&(handle)->cf_mutex);                                   \
      /* Loop through events, deallocating each after processing */           \
      while (!uv__queue_empty(&events)) {                                     \
        q = uv__queue_head(&events);                                          \
        event = uv__queue_data(q, uv__fsevents_event_t, member);              \
        uv__queue_remove(q);                                                  \
        /* NOTE: Checking uv__is_active() is required here, because handle    \
         * callback may close handle and invoking it after it will lead to    \
         * incorrect behaviour */                                             \
        if (!uv__is_closing((handle)) && uv__is_active((handle)))             \
          block                                                               \
        /* Free allocated data */                                             \
        uv__free(event);                                                      \
      }                                                                       \
      if (err != 0 && !uv__is_closing((handle)) && uv__is_active((handle)))   \
        (handle)->cb((handle), NULL, 0, err);                                 \
    } while (0)


/* Runs in UV loop's thread, when there're events to report to handle */
static void uv__fsevents_cb(uv_async_t* cb) {
  uv_fs_event_t* handle;

  handle = cb->data;

  UV__FSEVENTS_PROCESS(handle, {
    handle->cb(handle, event->path[0] ? event->path : NULL, event->events, 0);
  });
}


/* Runs in CF thread, pushed event into handle's event list */
static void uv__fsevents_push_event(uv_fs_event_t* handle,
                                    struct uv__queue* events,
                                    int err) {
  assert(events != NULL || err != 0);
  uv_mutex_lock(&handle->cf_mutex);

  /* Concatenate two queues */
  if (events != NULL)
    uv__queue_add(&handle->cf_events, events);

  /* Propagate error */
  if (err != 0)
    handle->cf_error = err;
  uv_mutex_unlock(&handle->cf_mutex);

  uv_async_send(handle->cf_cb);
}


/* Runs in CF thread, when there're events in FSEventStream */
static void uv__fsevents_event_cb(const FSEventStreamRef streamRef,
                                  void* info,
                                  size_t numEvents,
                                  void* eventPaths,
                                  const FSEventStreamEventFlags eventFlags[],
                                  const FSEventStreamEventId eventIds[]) {
  size_t i;
  int len;
  char** paths;
  char* path;
  char* pos;
  uv_fs_event_t* handle;
  struct uv__queue* q;
  uv_loop_t* loop;
  uv__cf_loop_state_t* state;
  uv__fsevents_event_t* event;
  FSEventStreamEventFlags flags;
  struct uv__queue head;
  uv__fsevents_handle_t* h;
  FSEventStreamEventId event_id;
  FSEventStreamEventId batch_id;
  int wrapped;
  int covered;

  loop = info;
  state = loop->cf_state;
  assert(state != NULL);
  paths = eventPaths;
  batch_id = 0;
  wrapped = 0;
  for (i = 0; i < numEvents; i++) {
    if (eventIds[i] > batch_id)
      batch_id = eventIds[i];
    if (eventFlags[i] & kFSEventStreamEventFlagEventIdsWrapped)
      wrapped = 1;
  }

  /* For each handle */
  uv_mutex_lock(&state->fsevent_mutex);
  uv__queue_foreach(q, &state->fsevent_handles) {
    handle = uv__queue_data(q, uv_fs_event_t, cf_member);
    uv__queue_init(&head);
    h = (uv__fsevents_handle_t*) handle->cf_cb;
    if (h->live_only) {
      if (streamRef != h->live_stream)
        continue;
    } else if (streamRef != state->fsevent_stream) {
      continue;
    }
    event_id = h->event_id;
    covered = h->live_only ||
              h->generation <= state->fsevent_stream_generation;

    /* Process and filter out events */
    for (i = 0; i < numEvents; i++) {
      flags = eventFlags[i];

      /* A wrapped ID invalidates the numeric replay boundary. Notify the
       * caller that it must rescan and rebase to the new epoch. Replaying
       * from zero could deliver history predating a newly admitted handle.
       */
      if (flags & kFSEventStreamEventFlagEventIdsWrapped) {
        h->event_id = pFSEventsGetCurrentEventId();
        event_id = h->event_id;
        uv__fsevents_push_event(handle, NULL, UV_ENOSPC);
        continue;
      }
      if (flags & kFSEventsSystem)
        continue;
      if (eventIds[i] <= h->event_id)
        continue;

      path = paths[i];
      len = strlen(path);

      if (handle->realpath_len == 0)
        continue; /* This should be unreachable */

      /* Filter out paths that are outside handle's request */
      if (len < handle->realpath_len)
        continue;

      /* Make sure that realpath actually named a directory,
       * (unless watching root, which alone keeps a trailing slash on the realpath)
       * or that we matched the whole string */
      if (handle->realpath_len != len &&
          handle->realpath_len > 1 &&
          path[handle->realpath_len] != '/')
        continue;

      if (memcmp(path, handle->realpath, handle->realpath_len) != 0)
        continue;

      if (!(handle->realpath_len == 1 && handle->realpath[0] == '/')) {
        /* Remove common prefix, unless the watched folder is "/" */
        path += handle->realpath_len;
        len -= handle->realpath_len;

        if (len == 0) {
          /* Since we're using fsevents to watch the file itself,
           * realpath == path, and we now need to get the basename of the file back
           * (for commonality with other codepaths and platforms). */
          while (len < handle->realpath_len && path[-1] != '/') {
            path--;
            len++;
          }
          /* Created and Removed seem to be always set, but don't make sense */
          flags &= ~kFSEventsRenamed;
        } else {
          /* Skip forward slash */
          path++;
          len--;
        }
      }

      /* Do not emit events from subdirectories (without option set) */
      if ((handle->cf_flags & UV_FS_EVENT_RECURSIVE) == 0 && *path != '\0') {
        pos = strchr(path + 1, '/');
        if (pos != NULL)
          continue;
      }

      event = uv__malloc(sizeof(*event) + len);
      if (event == NULL)
        break;

      memset(event, 0, sizeof(*event));
      memcpy(event->path, path, len + 1);
      event->events = UV_RENAME;

      if (0 == (flags & kFSEventsRenamed)) {
        if (0 != (flags & kFSEventsModified) ||
            0 == (flags & kFSEventStreamEventFlagItemIsDir))
          event->events = UV_CHANGE;
      }

      uv__queue_insert_tail(&head, &event->member);
      if (eventIds[i] > event_id)
        event_id = eventIds[i];
    }

    /* An older stream may cover only a child of a newly admitted watch.
     * It can deliver events, but must not advance that watch past unseen
     * siblings. Once fully covered, consume the entire batch, including
     * non-matching events, so quiet watches do not pin the replay history.
     */
    if (covered) {
      if (i == numEvents && !wrapped && batch_id > event_id)
        event_id = batch_id;
      h->event_id = event_id;
    }
    if (!uv__queue_empty(&head))
      uv__fsevents_push_event(handle, &head, 0);
  }
  uv_mutex_unlock(&state->fsevent_mutex);
}


/* Runs in CF thread */
static int uv__fsevents_create_stream(uv__cf_loop_state_t* state,
                                      uv_loop_t* loop,
                                      CFArrayRef paths,
                                      FSEventStreamEventId since,
                                      FSEventStreamRef* stream) {
  FSEventStreamContext ctx;
  FSEventStreamRef ref;
  CFAbsoluteTime latency;
  FSEventStreamCreateFlags flags;

  /* Initialize context */
  memset(&ctx, 0, sizeof(ctx));
  ctx.info = loop;

  latency = 0.05;

  /* Explanation of selected flags:
   * 1. NoDefer - without this flag, events that are happening continuously
   *    (i.e. each event is happening after time interval less than `latency`,
   *    counted from previous event), will be deferred and passed to callback
   *    once they'll either fill whole OS buffer, or when this continuous stream
   *    will stop (i.e. there'll be delay between events, bigger than
   *    `latency`).
   *    Specifying this flag will invoke callback after `latency` time passed
   *    since event.
   * 2. FileEvents - fire callback for file changes too (by default it is firing
   *    it only for directory changes).
   */
  flags = kFSEventStreamCreateFlagNoDefer | kFSEventStreamCreateFlagFileEvents;

  /* Replay from the oldest active handle's boundary. Each handle filters
   * its own history, so a newly added path cannot receive earlier events.
   */
  ref = pFSEventStreamCreate(NULL,
                             &uv__fsevents_event_cb,
                             &ctx,
                             paths,
                             since,
                             latency,
                             flags);
  assert(ref != NULL);

  pFSEventStreamScheduleWithRunLoop(ref, state->loop, *pkCFRunLoopDefaultMode);
  if (!pFSEventStreamStart(ref)) {
    pFSEventStreamInvalidate(ref);
    pFSEventStreamRelease(ref);
    return UV_EMFILE;
  }

  *stream = ref;
  return 0;
}


/* Runs in CF thread */
static void uv__fsevents_destroy_stream(FSEventStreamRef* stream) {
  if (*stream == NULL)
    return;

  /* Stop emitting events */
  pFSEventStreamStop(*stream);

  /* Release stream */
  pFSEventStreamInvalidate(*stream);
  pFSEventStreamRelease(*stream);
  *stream = NULL;
}


/* A device without an FSEvents UUID has no history. Keep its live stream
 * independent so other watches cannot interrupt it or force unsupported
 * history requests. Its initializer waits for native activation. */
static int uv__fsevents_create_live_stream(uv_loop_t* loop,
                                           uv_fs_event_t* handle) {
  uv__fsevents_handle_t* h;
  CFStringRef path;
  CFArrayRef paths;
  int err;

  h = (uv__fsevents_handle_t*) handle->cf_cb;
  path = pCFStringCreateWithFileSystemRepresentation(NULL, handle->realpath);
  if (path == NULL)
    return UV_ENOMEM;
  paths = pCFArrayCreate(NULL, (const void**) &path, 1, NULL);
  err = UV_ENOMEM;
  if (paths != NULL) {
    err = uv__fsevents_create_stream(loop->cf_state,
                                     loop,
                                     paths,
                                     kFSEventStreamEventIdSinceNow,
                                     &h->live_stream);
    pCFRelease(paths);
  }
  pCFRelease(path);
  return err;
}


/* Runs in CF thread, when there're new fsevent handles to add to stream */
static void uv__fsevents_reschedule(uv__cf_loop_state_t* state,
                                    uv_loop_t* loop) {
  struct uv__queue* q;
  uv_fs_event_t* curr;
  CFArrayRef cf_paths;
  CFStringRef* paths;
  unsigned int i;
  int err;
  unsigned int path_count;
  FSEventStreamEventId since;
  uv__fsevents_handle_t* h;
  uint64_t generation;

  paths = NULL;
  cf_paths = NULL;
  err = 0;
  /* NOTE: `i` is used in deallocation loop below */
  i = 0;
  since = kFSEventStreamEventIdSinceNow;

  /* Optimization to prevent O(n^2) time spent when starting to watch
   * many files simultaneously
   */
  uv_mutex_lock(&state->fsevent_mutex);
  if (state->fsevent_need_reschedule == 0) {
    uv_mutex_unlock(&state->fsevent_mutex);
    goto final;
  }
  state->fsevent_need_reschedule = 0;
  uv_mutex_unlock(&state->fsevent_mutex);

  /* Destroy previous FSEventStream */
  uv__fsevents_destroy_stream(&state->fsevent_stream);

  /* Any failure below will be a memory failure */
  err = UV_ENOMEM;

  /* Create list of all watched paths */
  uv_mutex_lock(&state->fsevent_mutex);
  path_count = state->fsevent_handle_count;
  generation = state->fsevent_generation;
  if (path_count != 0) {
    paths = uv__malloc(sizeof(*paths) * path_count);
    if (paths == NULL) {
      uv_mutex_unlock(&state->fsevent_mutex);
      goto final;
    }

    q = &state->fsevent_handles;
    for (; i < path_count; i++) {
      do {
        q = uv__queue_next(q);
        assert(q != &state->fsevent_handles);
        curr = uv__queue_data(q, uv_fs_event_t, cf_member);
        h = (uv__fsevents_handle_t*) curr->cf_cb;
      } while (h->live_only);
      if (h->event_id < since)
        since = h->event_id;
      assert(curr->realpath != NULL);
      paths[i] =
          pCFStringCreateWithFileSystemRepresentation(NULL, curr->realpath);
      if (paths[i] == NULL) {
        uv_mutex_unlock(&state->fsevent_mutex);
        goto final;
      }
    }
  }
  uv_mutex_unlock(&state->fsevent_mutex);
  err = 0;

  if (path_count != 0) {
    /* Create new FSEventStream */
    cf_paths = pCFArrayCreate(NULL, (const void**) paths, path_count, NULL);
    if (cf_paths == NULL) {
      err = UV_ENOMEM;
      goto final;
    }
    err = uv__fsevents_create_stream(state,
                                     loop,
                                     cf_paths,
                                     since,
                                     &state->fsevent_stream);
    state->fsevent_stream_generation = generation;
  }

final:
  /* Deallocate all paths in case of failure */
  if (err != 0) {
    if (cf_paths == NULL) {
      while (i != 0)
        pCFRelease(paths[--i]);
      uv__free(paths);
    } else {
      /* CFArray takes ownership of both strings and original C-array */
      pCFRelease(cf_paths);
    }

    /* Broadcast error to all handles */
    uv_mutex_lock(&state->fsevent_mutex);
    uv__queue_foreach(q, &state->fsevent_handles) {
      curr = uv__queue_data(q, uv_fs_event_t, cf_member);
      h = (uv__fsevents_handle_t*) curr->cf_cb;
      if (!h->live_only)
        uv__fsevents_push_event(curr, NULL, err);
    }
    uv_mutex_unlock(&state->fsevent_mutex);
  }
}


static int uv__fsevents_global_init(void) {
  static pthread_mutex_t global_init_mutex = PTHREAD_MUTEX_INITIALIZER;
  static void* core_foundation_handle;
  static void* core_services_handle;
  int err;

  err = 0;
  pthread_mutex_lock(&global_init_mutex);
  if (core_foundation_handle != NULL)
    goto out;

  /* The libraries are never unloaded because we currently don't have a good
   * mechanism for keeping a reference count. It's unlikely to be an issue
   * but if it ever becomes one, we can turn the dynamic library handles into
   * per-event loop properties and have the dynamic linker keep track for us.
   */
  err = UV_ENOSYS;
  core_foundation_handle = dlopen("/System/Library/Frameworks/"
                                  "CoreFoundation.framework/"
                                  "Versions/A/CoreFoundation",
                                  RTLD_LAZY | RTLD_LOCAL);
  if (core_foundation_handle == NULL)
    goto out;

  core_services_handle = dlopen("/System/Library/Frameworks/"
                                "CoreServices.framework/"
                                "Versions/A/CoreServices",
                                RTLD_LAZY | RTLD_LOCAL);
  if (core_services_handle == NULL)
    goto out;

  err = UV_ENOENT;
#define V(handle, symbol)                                                     \
  do {                                                                        \
    *(void **)(&p ## symbol) = dlsym((handle), #symbol);                      \
    if (p ## symbol == NULL)                                                  \
      goto out;                                                               \
  }                                                                           \
  while (0)
  V(core_foundation_handle, CFArrayCreate);
  V(core_foundation_handle, CFRelease);
  V(core_foundation_handle, CFRunLoopAddSource);
  V(core_foundation_handle, CFRunLoopGetCurrent);
  V(core_foundation_handle, CFRunLoopRemoveSource);
  V(core_foundation_handle, CFRunLoopRun);
  V(core_foundation_handle, CFRunLoopSourceCreate);
  V(core_foundation_handle, CFRunLoopSourceSignal);
  V(core_foundation_handle, CFRunLoopStop);
  V(core_foundation_handle, CFRunLoopWakeUp);
  V(core_foundation_handle, CFStringCreateWithFileSystemRepresentation);
  V(core_foundation_handle, kCFRunLoopDefaultMode);
  V(core_services_handle, FSEventStreamCreate);
  V(core_services_handle, FSEventStreamInvalidate);
  V(core_services_handle, FSEventStreamRelease);
  V(core_services_handle, FSEventStreamScheduleWithRunLoop);
  V(core_services_handle, FSEventStreamStart);
  V(core_services_handle, FSEventStreamStop);
  V(core_services_handle, FSEventsGetCurrentEventId);
  V(core_services_handle, FSEventsCopyUUIDForDevice);
#undef V
  err = 0;

out:
  if (err && core_services_handle != NULL) {
    dlclose(core_services_handle);
    core_services_handle = NULL;
  }

  if (err && core_foundation_handle != NULL) {
    dlclose(core_foundation_handle);
    core_foundation_handle = NULL;
  }

  pthread_mutex_unlock(&global_init_mutex);
  return err;
}


/* Runs in UV loop */
static int uv__fsevents_loop_init(uv_loop_t* loop) {
  CFRunLoopSourceContext ctx;
  uv__cf_loop_state_t* state;
  pthread_attr_t attr;
  int err;

  if (loop->cf_state != NULL)
    return 0;

  err = uv__fsevents_global_init();
  if (err)
    return err;

  state = uv__calloc(1, sizeof(*state));
  if (state == NULL)
    return UV_ENOMEM;

  err = uv_mutex_init(&loop->cf_mutex);
  if (err)
    goto fail_mutex_init;

  err = uv_sem_init(&loop->cf_sem, 0);
  if (err)
    goto fail_sem_init;

  uv__queue_init(&loop->cf_signals);

  err = uv_cond_init(&state->fsevent_cond);
  if (err)
    goto fail_fsevent_cond_init;

  err = uv_mutex_init(&state->fsevent_mutex);
  if (err)
    goto fail_fsevent_mutex_init;

  uv__queue_init(&state->fsevent_handles);
  state->fsevent_need_reschedule = 0;
  state->fsevent_handle_count = 0;

  memset(&ctx, 0, sizeof(ctx));
  ctx.info = loop;
  ctx.perform = uv__cf_loop_cb;
  state->signal_source = pCFRunLoopSourceCreate(NULL, 0, &ctx);
  if (state->signal_source == NULL) {
    err = UV_ENOMEM;
    goto fail_signal_source_create;
  }

  if (pthread_attr_init(&attr))
    abort();

  if (pthread_attr_setstacksize(&attr, uv__thread_stack_size()))
    abort();

  loop->cf_state = state;

  /* uv_thread_t is an alias for pthread_t. */
  err = UV__ERR(pthread_create(&loop->cf_thread, &attr, uv__cf_loop_runner, loop));

  if (pthread_attr_destroy(&attr))
    abort();

  if (err)
    goto fail_thread_create;

  /* Synchronize threads */
  uv_sem_wait(&loop->cf_sem);
  return 0;

fail_thread_create:
  loop->cf_state = NULL;

fail_signal_source_create:
  uv_mutex_destroy(&state->fsevent_mutex);

fail_fsevent_mutex_init:
  uv_cond_destroy(&state->fsevent_cond);

fail_fsevent_cond_init:
  uv_sem_destroy(&loop->cf_sem);

fail_sem_init:
  uv_mutex_destroy(&loop->cf_mutex);

fail_mutex_init:
  uv__free(state);
  return err;
}


/* Runs in UV loop */
void uv__fsevents_loop_delete(uv_loop_t* loop) {
  uv__cf_loop_signal_t* s;
  uv__cf_loop_state_t* state;
  struct uv__queue* q;

  if (loop->cf_state == NULL)
    return;

  if (uv__cf_loop_signal(loop, NULL) != 0)
    abort();

  uv_thread_join(&loop->cf_thread);
  uv_sem_destroy(&loop->cf_sem);
  uv_mutex_destroy(&loop->cf_mutex);

  /* Free any remaining data */
  while (!uv__queue_empty(&loop->cf_signals)) {
    q = uv__queue_head(&loop->cf_signals);
    s = uv__queue_data(q, uv__cf_loop_signal_t, member);
    uv__queue_remove(q);
    uv__free(s);
  }

  /* Destroy state */
  state = loop->cf_state;
  uv_cond_destroy(&state->fsevent_cond);
  uv_mutex_destroy(&state->fsevent_mutex);
  pCFRelease(state->signal_source);
  uv__free(state);
  loop->cf_state = NULL;
}


/* Runs in CF thread. This is the CF loop's body */
static void* uv__cf_loop_runner(void* arg) {
  uv_loop_t* loop;
  uv__cf_loop_state_t* state;

  loop = arg;
  state = loop->cf_state;
  state->loop = pCFRunLoopGetCurrent();

  pCFRunLoopAddSource(state->loop,
                      state->signal_source,
                      *pkCFRunLoopDefaultMode);

  uv_sem_post(&loop->cf_sem);

  pCFRunLoopRun();
  pCFRunLoopRemoveSource(state->loop,
                         state->signal_source,
                         *pkCFRunLoopDefaultMode);

  state->loop = NULL;

  return NULL;
}


/* Runs in CF thread, executed after `uv__cf_loop_signal()` */
static void uv__cf_loop_cb(void* arg) {
  uv_loop_t* loop;
  uv__cf_loop_state_t* state;
  struct uv__queue* item;
  struct uv__queue split_head;
  uv__cf_loop_signal_t* s;
  uv__fsevents_handle_t* h;
  uv_fs_event_t* handle;
  uv__cf_loop_signal_type_t type;
  int err;

  loop = arg;
  state = loop->cf_state;

  uv_mutex_lock(&loop->cf_mutex);
  uv__queue_move(&loop->cf_signals, &split_head);
  uv_mutex_unlock(&loop->cf_mutex);

  while (!uv__queue_empty(&split_head)) {
    item = uv__queue_head(&split_head);
    uv__queue_remove(item);

    s = uv__queue_data(item, uv__cf_loop_signal_t, member);
    handle = s->handle;
    type = s->type;
    /* Closing signals live on the waiting UV thread's stack. Do not access
     * the signal again after publishing completion below.
     */
    if (type != kUVCFLoopSignalClosing)
      uv__free(s);

    /* This was a termination signal */
    if (handle == NULL) {
      pCFRunLoopStop(state->loop);
    } else {
      h = (uv__fsevents_handle_t*) handle->cf_cb;
      if (!h->live_only) {
        uv__fsevents_reschedule(state, loop);
      } else if (type == kUVCFLoopSignalClosing) {
        uv__fsevents_destroy_stream(&h->live_stream);
      } else {
        err = uv__fsevents_create_live_stream(loop, handle);
        if (err != 0)
          uv__fsevents_push_event(handle, NULL, err);
        else
          /* Live-only admission completes after native activation. Reject
           * events queued during activation, before start returns.
           */
          h->event_id = pFSEventsGetCurrentEventId();
      }

      /* Publish completion after the last access to the handle. The UV
       * thread may then finish admission or reclaim the closing handle.
       */
      if (h->live_only || type == kUVCFLoopSignalClosing) {
        uv_mutex_lock(&state->fsevent_mutex);
        h->ready = 1;
        uv_cond_signal(&state->fsevent_cond);
        uv_mutex_unlock(&state->fsevent_mutex);
      }
    }
  }
}


/* Runs in UV loop to publish an owned signal to the CF thread. */
static void uv__cf_loop_send(uv_loop_t* loop, uv__cf_loop_signal_t* item) {
  uv__cf_loop_state_t* state;

  uv_mutex_lock(&loop->cf_mutex);
  uv__queue_insert_tail(&loop->cf_signals, &item->member);

  state = loop->cf_state;
  assert(state != NULL);
  pCFRunLoopSourceSignal(state->signal_source);
  pCFRunLoopWakeUp(state->loop);

  uv_mutex_unlock(&loop->cf_mutex);
}


/* Runs in UV loop to notify CF thread asynchronously. */
int uv__cf_loop_signal(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv__cf_loop_signal_t* item;

  item = uv__malloc(sizeof(*item));
  if (item == NULL)
    return UV_ENOMEM;

  item->handle = handle;
  item->type = kUVCFLoopSignalRegular;
  uv__cf_loop_send(loop, item);
  return 0;
}


/* Runs in UV loop to initialize handle */
int uv__fsevents_init(uv_fs_event_t* handle) {
  char* buf;
  int err;
  int live_only;
  uv__cf_loop_state_t* state;
  uv__fsevents_handle_t* h;
  struct stat statbuf;
  CFTypeRef uuid;

  err = uv__fsevents_loop_init(handle->loop);
  if (err)
    return err;

  /* Get absolute path to file */
  buf = realpath(handle->path, NULL);
  if (buf == NULL)
    return UV__ERR(errno);
  handle->realpath = uv__strdup(buf);
  free(buf); /* _Not_ uv__free. */
  if (handle->realpath == NULL)
    return UV_ENOMEM;
  handle->realpath_len = strlen(handle->realpath);

  if (stat(handle->realpath, &statbuf) != 0) {
    err = UV__ERR(errno);
    goto fail_cf_cb_malloc;
  }
  uuid = pFSEventsCopyUUIDForDevice(statbuf.st_dev);
  live_only = uuid == NULL;
  if (uuid != NULL)
    pCFRelease(uuid);

  /* Initialize event queue */
  uv__queue_init(&handle->cf_events);
  handle->cf_error = 0;

  /*
   * Events will occur in other thread.
   * Initialize callback for getting them back into event loop's thread
   */
  handle->cf_cb = uv__malloc(sizeof(uv__fsevents_handle_t));
  if (handle->cf_cb == NULL) {
    err = UV_ENOMEM;
    goto fail_cf_cb_malloc;
  }

  h = (uv__fsevents_handle_t*) handle->cf_cb;
  h->live_only = live_only;
  h->live_stream = NULL;
  h->ready = 0;

  handle->cf_cb->data = handle;
  uv_async_init(handle->loop, handle->cf_cb, uv__fsevents_cb);
  handle->cf_cb->flags |= UV_HANDLE_INTERNAL;
  uv_unref((uv_handle_t*) handle->cf_cb);

  err = uv_mutex_init(&handle->cf_mutex);
  if (err)
    goto fail_cf_mutex_init;

  /* Capture admission before publishing the handle or queuing CF work.
   * A delayed stream creation must replay events after this boundary.
   */
  h->event_id = pFSEventsGetCurrentEventId();

  /* Insert handle into the list */
  state = handle->loop->cf_state;
  uv_mutex_lock(&state->fsevent_mutex);
  h->generation = ++state->fsevent_generation;
  uv__queue_insert_tail(&state->fsevent_handles, &handle->cf_member);
  if (!live_only) {
    state->fsevent_handle_count++;
    state->fsevent_need_reschedule = 1;
  }
  uv_mutex_unlock(&state->fsevent_mutex);

  /* Reschedule FSEventStream */
  assert(handle != NULL);
  err = uv__cf_loop_signal(handle->loop, handle);
  if (err)
    goto fail_loop_signal;

  if (live_only) {
    uv_mutex_lock(&state->fsevent_mutex);
    while (!h->ready)
      uv_cond_wait(&state->fsevent_cond, &state->fsevent_mutex);
    uv_mutex_unlock(&state->fsevent_mutex);
  }
  return 0;

fail_loop_signal:
  uv_mutex_destroy(&handle->cf_mutex);

fail_cf_mutex_init:
  uv__free(handle->cf_cb);
  handle->cf_cb = NULL;

fail_cf_cb_malloc:
  uv__free(handle->realpath);
  handle->realpath = NULL;
  handle->realpath_len = 0;

  return err;
}


/* Runs in UV loop to de-initialize handle */
int uv__fsevents_close(uv_fs_event_t* handle) {
  uv__cf_loop_state_t* state;
  uv__fsevents_handle_t* h;
  uv__cf_loop_signal_t signal;

  if (handle->cf_cb == NULL)
    return UV_EINVAL;

  /* Remove handle from the list. */
  state = handle->loop->cf_state;
  h = (uv__fsevents_handle_t*) handle->cf_cb;
  uv_mutex_lock(&state->fsevent_mutex);
  h->ready = 0;
  uv__queue_remove(&handle->cf_member);
  if (!h->live_only) {
    state->fsevent_handle_count--;
    state->fsevent_need_reschedule = 1;
  }
  uv_mutex_unlock(&state->fsevent_mutex);

  /* Closing must drain queued references even when allocation fails. The
   * stack signal remains valid until the CF thread acknowledges completion.
   */
  signal.handle = handle;
  signal.type = kUVCFLoopSignalClosing;
  uv__cf_loop_send(handle->loop, &signal);

  /* Wait for the CF thread to release its last reference to the handle. */
  uv_mutex_lock(&state->fsevent_mutex);
  while (!h->ready)
    uv_cond_wait(&state->fsevent_cond, &state->fsevent_mutex);
  uv_mutex_unlock(&state->fsevent_mutex);

  uv_close((uv_handle_t*) handle->cf_cb, (uv_close_cb) uv__free);
  handle->cf_cb = NULL;

  /* Free data in queue */
  UV__FSEVENTS_PROCESS(handle, {
    /* NOP */
  });

  uv_mutex_destroy(&handle->cf_mutex);
  uv__free(handle->realpath);
  handle->realpath = NULL;
  handle->realpath_len = 0;

  return 0;
}

#endif /* TARGET_OS_IPHONE */
