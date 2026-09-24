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
#include "task.h"

#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
/* Deterministic native qualification: include the actual implementation so
 * its lazy-loaded native call pointers can be gated without production hooks.
 * All notifications are real filesystem events; the observer only orders them.
 */
#include "../src/unix/fsevents.c"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

void uv__test_fsevents_observer_start(const char*);
uint64_t uv__test_fsevents_observer_mutate(const char*);
void uv__test_fsevents_observer_stop(void);

static uv_mutex_t gate_mutex;
static uv_cond_t gate_cond;
static int starts;
static int gate;
static char mutation[4096];
static char mutation2[4096];
static uint64_t mutation_at;
static int wanted_mask;
static int seen_mask;
static int seed_seen;
static int seed_phase;
static int stale_handle;
static int overlap;
static int native_seen;
static uint64_t native_seen_id;
static char native_wait_path[4096];
static uint64_t minimum_since;
static int unsafe_replay;
static int live_create_expected;
static uint64_t mutation_id;
static int wanted;
static int stale;
static int expired;
static uv_timer_t deadline;
static uv_loop_t test_loop;
static uv_fs_event_t a, b, c;
static void (*real_signal)(CFRunLoopSourceRef);
static CFTypeRef (*real_history)(dev_t);
static void (*real_stop)(FSEventStreamRef);
static int (*real_start)(FSEventStreamRef);
static FSEventStreamRef (*real_create)(CFAllocatorRef, FSEventStreamCallback,
    FSEventStreamContext*, CFArrayRef, FSEventStreamEventId, CFTimeInterval,
    FSEventStreamCreateFlags);

static void wait_native(uint64_t end) {
  int rc = 0;
  uv_mutex_lock(&gate_mutex);
  while (!native_seen && rc == 0) {
    uint64_t now = uv_hrtime();
    if (now >= end)
      break;
    rc = uv_cond_timedwait(&gate_cond, &gate_mutex, end - now);
  }
  ASSERT(native_seen);
  uv_mutex_unlock(&gate_mutex);
}

static void checkpoint(int point) {
  int fire;
  uv_mutex_lock(&gate_mutex);
  fire = gate == point;
  if (fire)
    gate = 0;
  uv_mutex_unlock(&gate_mutex);
  if (fire) {
    mutation_at = uv_hrtime();
    mutation_id = uv__test_fsevents_observer_mutate(mutation);
    if (mutation2[0] != '\0')
      uv__test_fsevents_observer_mutate(mutation2);
    if (native_wait_path[0] != '\0')
      wait_native(mutation_at + 3000000000ULL);
  }
}
static void gated_signal(CFRunLoopSourceRef ref) {
  checkpoint(1);
  real_signal(ref);
}
static void gated_stop(FSEventStreamRef ref) {
  checkpoint(2);
  real_stop(ref);
}
static int gated_start(FSEventStreamRef ref) {
  int rc = real_start(ref);
  uv_mutex_lock(&gate_mutex);
  starts++;
  uv_cond_signal(&gate_cond);
  uv_mutex_unlock(&gate_mutex);
  return rc;
}
static void native_callback(const FSEventStreamRef stream, void* info,
                            size_t count, void* paths,
                            const FSEventStreamEventFlags flags[],
                            const FSEventStreamEventId ids[]) {
  size_t i;
  char** names = paths;

  uv__fsevents_event_cb(stream, info, count, paths, flags, ids);
  uv_mutex_lock(&gate_mutex);
  for (i = 0; i < count; i++) {
    if (!(flags[i] & kFSEventsSystem) &&
        native_wait_path[0] != '\0' &&
        strcmp(names[i], native_wait_path) == 0) {
      native_seen = 1;
      if (ids[i] > native_seen_id)
        native_seen_id = ids[i];
      uv_cond_broadcast(&gate_cond);
    }
  }
  uv_mutex_unlock(&gate_mutex);
}
static FSEventStreamRef gated_create(CFAllocatorRef allocator,
    FSEventStreamCallback cb, FSEventStreamContext* context, CFArrayRef paths,
    FSEventStreamEventId since, CFTimeInterval latency,
    FSEventStreamCreateFlags flags) {
  checkpoint(3);
  fprintf(stderr, "create since=%llu\n", (unsigned long long) since);
  uv_mutex_lock(&gate_mutex);
  if (minimum_since != 0 && since < minimum_since)
    unsafe_replay++;
  if (live_create_expected) {
    ASSERT(since == kFSEventStreamEventIdSinceNow);
    live_create_expected = 0;
  }
  uv_mutex_unlock(&gate_mutex);
  ASSERT(cb == uv__fsevents_event_cb);
  return real_create(allocator, native_callback, context, paths,
                     since, latency, flags);
}
static void arm(int point, const char* path) {
  uv_mutex_lock(&gate_mutex);
  ASSERT(strlen(path) < sizeof(mutation));
  strcpy(mutation, path);
  mutation_id = 0;
  mutation2[0] = '\0';
  gate = point;
  uv_mutex_unlock(&gate_mutex);
}
static void wait_started(int count) {
  uint64_t end = uv_hrtime() + 3000000000ULL;
  int rc = 0;
  uv_mutex_lock(&gate_mutex);
  while (starts < count && rc == 0) {
    uint64_t now = uv_hrtime();
    if (now >= end)
      break;
    rc = uv_cond_timedwait(&gate_cond, &gate_mutex, end - now);
  }
  ASSERT(starts >= count);
  uv_mutex_unlock(&gate_mutex);
}
static void on_event(uv_fs_event_t* handle, const char* path, int events,
                     int status) {
  ASSERT(status == 0);
  fprintf(stderr, "libuv handle=%c path=%s events=%d\n",
          handle == &a ? 'A' : 'B', path == NULL ? "NULL" : path, events);
  if (path != NULL && strcmp(path, "preadmission") == 0 &&
      (stale_handle == 0 || handle == &b)) stale++;
  if (seed_phase && path != NULL && strcmp(path, "seed") == 0) {
    seed_seen++;
    uv_stop(&test_loop);
  }
  if (overlap && handle == &a && path != NULL &&
      strcmp(path, "inside") == 0) {
    seen_mask |= 1;
    if ((seen_mask & wanted_mask) == wanted_mask)
      uv_stop(&test_loop);
  }
  if (path != NULL && strcmp(path, "admitted") == 0) {
    ASSERT(events & (UV_RENAME | UV_CHANGE));
    wanted++;
    seen_mask |= handle == &a ? 1 : 2;
    if ((seen_mask & wanted_mask) == wanted_mask) uv_stop(&test_loop);
  }
}
/* Fault-inject only history availability, not filesystem notifications. */
static CFTypeRef no_history(dev_t device) {
  (void) device;
  return NULL;
}

static void start_live(uv_fs_event_t* handle, const char* path) {
  int before;

  uv_mutex_lock(&gate_mutex);
  before = starts;
  live_create_expected = 1;
  uv_mutex_unlock(&gate_mutex);
  pFSEventsCopyUUIDForDevice = no_history;
  ASSERT(uv_fs_event_start(handle, on_event, path, 0) == 0);
  pFSEventsCopyUUIDForDevice = real_history;
  uv_mutex_lock(&gate_mutex);
  ASSERT(live_create_expected == 0);
  ASSERT(starts == before + 1);
  uv_mutex_unlock(&gate_mutex);
}

static void timed_out(uv_timer_t* timer) {
  (void) timer;
  expired = 1;
  uv_stop(&test_loop);
}
static int run_case(const char* mode) {
  char root[] = "/private/tmp/libuv3866-XXXXXX";
  char dir_a[4096], dir_b[4096], path[4096], old[4096];
  char barrier[4096], seed[4096], path_b[4096], inside[4096];
  uint64_t seed_end;
  uint64_t elapsed;
  uint64_t remaining;
  int ok;
  ASSERT(mkdtemp(root) != NULL);
  snprintf(dir_a, sizeof(dir_a), "%s/a", root);
  snprintf(dir_b, sizeof(dir_b), "%s/b", root);
  ASSERT(mkdir(dir_a, 0700) == 0);
  ASSERT(mkdir(dir_b, 0700) == 0);
  snprintf(path, sizeof(path), "%s/admitted", dir_a);
  snprintf(old, sizeof(old), "%s/preadmission", dir_a);
  snprintf(barrier, sizeof(barrier), "%s/barrier", root);
  snprintf(seed, sizeof(seed), "%s/seed", dir_a);
  snprintf(path_b, sizeof(path_b), "%s/admitted", dir_b);
  snprintf(inside, sizeof(inside), "%s/inside", dir_a);
  uv__test_fsevents_observer_start(root);
  /* Native acknowledgment orders setup before admission without a sleep. */
  uv__test_fsevents_observer_mutate(barrier);
  wanted_mask = 1;
  ASSERT(uv_mutex_init(&gate_mutex) == 0);
  ASSERT(uv_cond_init(&gate_cond) == 0);
  ASSERT(uv__fsevents_global_init() == 0);
  real_signal = pCFRunLoopSourceSignal;
  real_history = pFSEventsCopyUUIDForDevice;
  real_stop = pFSEventStreamStop;
  real_start = pFSEventStreamStart;
  real_create = pFSEventStreamCreate;
  pCFRunLoopSourceSignal = gated_signal;
  pFSEventStreamStop = gated_stop;
  pFSEventStreamStart = gated_start;
  pFSEventStreamCreate = gated_create;
  ASSERT(uv_loop_init(&test_loop) == 0);
  ASSERT(uv_fs_event_init(&test_loop, &a) == 0);
  ASSERT(uv_fs_event_init(&test_loop, &b) == 0);
  ASSERT(uv_fs_event_init(&test_loop, &c) == 0);
  ASSERT(uv_timer_init(&test_loop, &deadline) == 0);
  if (strcmp(mode, "preadmission") == 0) uv__test_fsevents_observer_mutate(old);
  if (strcmp(mode, "coalesced") == 0) uv__test_fsevents_observer_mutate(path);
  if (strcmp(mode, "signal") == 0 || strcmp(mode, "preadmission") == 0 ||
      strcmp(mode, "coalesced") == 0)
    arm(1, path);
  if (strcmp(mode, "create") == 0) arm(3, path);
  if (strcmp(mode, "live") == 0)
    arm(3, barrier);
  if (strncmp(mode, "live", 4) == 0)
    start_live(&a, dir_a);
  else
    ASSERT(uv_fs_event_start(&a, on_event, dir_a, 0) == 0);
  wait_started(1);
  if (strcmp(mode, "quiet") == 0) {
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
  }
  if (strcmp(mode, "pending-add") == 0 ||
      strcmp(mode, "two-floors") == 0 || strcmp(mode, "quiet") == 0) {
    seed_phase = 1;
    seed_end = uv_hrtime() + 3000000000ULL;
    if (strcmp(mode, "quiet") == 0) {
      uv_mutex_lock(&gate_mutex);
      strcpy(native_wait_path, seed);
      uv_mutex_unlock(&gate_mutex);
    }
    uv__test_fsevents_observer_mutate(seed);
    ASSERT(uv_timer_start(&deadline, timed_out, 3000, 0) == 0);
    uv_run(&test_loop, UV_RUN_DEFAULT);
    ASSERT(seed_seen > 0 && !expired);
    ASSERT(uv_timer_stop(&deadline) == 0);
    seed_phase = 0;
    if (strcmp(mode, "quiet") == 0) {
      /* The independent observer may coalesce a later ID. Compare only
       * against a native seed event that libuv has actually consumed. */
      wait_native(seed_end);
      uv_mutex_lock(&gate_mutex);
      ASSERT(native_seen_id != 0);
      minimum_since = native_seen_id;
      native_wait_path[0] = '\0';
      uv_mutex_unlock(&gate_mutex);
    }
  }
  if (strcmp(mode, "live-reacquire") == 0) {
    ASSERT(uv_fs_event_stop(&a) == 0);
    uv__test_fsevents_observer_mutate(old);
    start_live(&a, dir_a);
    mutation_at = uv_hrtime();
    mutation_id = uv__test_fsevents_observer_mutate(path);
  } else if (strcmp(mode, "mixed-add") == 0) {
    arm(3, path);
    start_live(&b, dir_b);
  } else if (strcmp(mode, "mixed-remove") == 0) {
    start_live(&b, dir_b);
    arm(2, path);
    ASSERT(uv_fs_event_stop(&b) == 0);
  } else if (strcmp(mode, "live-add") == 0) {
    arm(3, path);
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
  } else if (strcmp(mode, "live-remove") == 0) {
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
    arm(2, path);
    ASSERT(uv_fs_event_stop(&b) == 0);
  } else if (strcmp(mode, "two-floors") == 0) {
    snprintf(old, sizeof(old), "%s/preadmission", dir_b);
    uv__test_fsevents_observer_mutate(old);
    stale_handle = 1;
    wanted_mask = 3;
    arm(2, path);
    uv_mutex_lock(&gate_mutex);
    strcpy(mutation2, path_b);
    uv_mutex_unlock(&gate_mutex);
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
  } else if (strcmp(mode, "overlap") == 0) {
    overlap = 1;
    wanted_mask = 3;
    snprintf(path, sizeof(path), "%s/admitted", root);
    arm(1, path);
    uv_mutex_lock(&gate_mutex);
    strcpy(mutation2, inside);
    strcpy(native_wait_path, inside);
    uv_mutex_unlock(&gate_mutex);
    ASSERT(uv_fs_event_start(&b, on_event, root, UV_FS_EVENT_RECURSIVE) == 0);
    wait_started(2);
  } else if (strcmp(mode, "quiet") == 0) {
    arm(2, path);
    ASSERT(uv_fs_event_start(&c, on_event, dir_b, 0) == 0);
    wait_started(3);
  } else if (strcmp(mode, "reacquire") == 0) {
    ASSERT(uv_fs_event_stop(&a) == 0);
    uv__test_fsevents_observer_mutate(old);
    arm(1, path);
    ASSERT(uv_fs_event_start(&a, on_event, dir_a, 0) == 0);
    wait_started(2);
  } else if (strcmp(mode, "add") == 0 || strcmp(mode, "handover") == 0 ||
             strcmp(mode, "pending-add") == 0) {
    arm(strcmp(mode, "handover") == 0 ? 3 : 2, path);
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
  } else if (strcmp(mode, "remove") == 0) {
    ASSERT(uv_fs_event_start(&b, on_event, dir_b, 0) == 0);
    wait_started(2);
    arm(2, path);
    ASSERT(uv_fs_event_stop(&b) == 0);
    wait_started(3);
  } else if (strcmp(mode, "control") == 0 || strcmp(mode, "live") == 0) {
    mutation_at = uv_hrtime();
    mutation_id = uv__test_fsevents_observer_mutate(path);
  }
  ASSERT(mutation_id != 0);
  uv_update_time(&test_loop);
  elapsed = uv_hrtime() - mutation_at;
  remaining = elapsed < 3000000000ULL ?
      (3000000000ULL - elapsed) / 1000000 : 0;
  ASSERT(uv_timer_start(&deadline, timed_out, remaining, 0) == 0);
  uv_run(&test_loop, UV_RUN_DEFAULT);
  elapsed = uv_hrtime() - mutation_at;
  ok = (seen_mask & wanted_mask) == wanted_mask && !expired && stale == 0 &&
       elapsed <= 3000000000ULL && unsafe_replay == 0;
  uv_close((uv_handle_t*) &deadline, NULL);
  uv_close((uv_handle_t*) &a, NULL);
  uv_close((uv_handle_t*) &b, NULL);
  uv_close((uv_handle_t*) &c, NULL);
  ASSERT(uv_run(&test_loop, UV_RUN_DEFAULT) == 0);
  ASSERT(uv_loop_close(&test_loop) == 0);
  uv__test_fsevents_observer_stop();
  pCFRunLoopSourceSignal = real_signal;
  pFSEventsCopyUUIDForDevice = real_history;
  pFSEventStreamStop = real_stop;
  pFSEventStreamStart = real_start;
  pFSEventStreamCreate = real_create;
  uv_cond_destroy(&gate_cond);
  uv_mutex_destroy(&gate_mutex);
  unlink(path);
  unlink(old);
  unlink(barrier);
  unlink(seed);
  unlink(path_b);
  unlink(inside);
  ASSERT(rmdir(dir_a) == 0);
  ASSERT(rmdir(dir_b) == 0);
  ASSERT(rmdir(root) == 0);
  printf("{\"mode\":\"%s\",\"pass\":%s,\"admitted\":%d,"
         "\"preadmission\":%d,\"expired\":%d,\"nativeId\":%llu,"
         "\"elapsedNs\":%llu,\"unsafeReplay\":%d}\n",
         mode, ok ? "true" : "false", wanted, stale, expired,
         (unsigned long long) mutation_id, (unsigned long long) elapsed,
         unsafe_replay);
  uv_library_shutdown();
  return ok ? 0 : 2;
}

static int native_start_fails(FSEventStreamRef stream) {
  (void) stream;
  return 0;
}

static void on_start_error(uv_fs_event_t* handle, const char* path,
                            int events, int status) {
  ASSERT(handle == &a);
  ASSERT(path == NULL);
  ASSERT(events == 0);
  ASSERT(status == UV_EMFILE);
  wanted++;
  uv_close((uv_handle_t*) handle, NULL);
  uv_close((uv_handle_t*) &deadline, NULL);
}

static int run_live_failure(void) {
  char root[] = "live-failure-XXXXXX";

  ASSERT(mkdtemp(root) != NULL);
  ASSERT(uv__fsevents_global_init() == 0);
  real_history = pFSEventsCopyUUIDForDevice;
  real_start = pFSEventStreamStart;
  pFSEventsCopyUUIDForDevice = no_history;
  pFSEventStreamStart = native_start_fails;
  ASSERT(uv_loop_init(&test_loop) == 0);
  ASSERT(uv_fs_event_init(&test_loop, &a) == 0);
  ASSERT(uv_timer_init(&test_loop, &deadline) == 0);
  ASSERT(uv_timer_start(&deadline, timed_out, 3000, 0) == 0);
  /* Preserve the existing asynchronous native-start error contract, while
   * ensuring the live-only admission wait is released on failure too. */
  ASSERT(uv_fs_event_start(&a, on_start_error, root, 0) == 0);
  ASSERT(wanted == 0);
  ASSERT(uv_run(&test_loop, UV_RUN_DEFAULT) == 0);
  ASSERT(wanted == 1 && !expired);
  ASSERT(uv_loop_close(&test_loop) == 0);
  pFSEventsCopyUUIDForDevice = real_history;
  pFSEventStreamStart = real_start;
  ASSERT(rmdir(root) == 0);
  uv_library_shutdown();
  return 0;
}

#endif

TEST_IMPL(fs_event_admission_control) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("control");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_signal) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("signal");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_create) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("create");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_add) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("add");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_remove) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("remove");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_handover) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("handover");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_reacquire) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("reacquire");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_preadmission) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("preadmission");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_pending_add) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("pending-add");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_two_floors) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("two-floors");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_coalesced) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("coalesced");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}


TEST_IMPL(fs_event_admission_overlap) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("overlap");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_quiet) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("quiet");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_live) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("live");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_live_add) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("live-add");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_live_remove) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("live-remove");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_mixed_add) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("mixed-add");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_mixed_remove) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("mixed-remove");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_live_reacquire) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_case("live-reacquire");
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}

TEST_IMPL(fs_event_admission_live_failure) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE && defined(UV_TEST_FSEVENTS)
  return run_live_failure();
#else
  RETURN_SKIP("Requires the macOS static FSEvents test build.");
#endif
}
