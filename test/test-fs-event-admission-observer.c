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

#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>


static FSEventStreamRef observer;
static dispatch_queue_t queue;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static char expected[4096];
static uint64_t observed;
static uint64_t floor_id;

static void event_cb(ConstFSEventStreamRef stream, void* info, size_t n,
                     void* paths, const FSEventStreamEventFlags flags[],
                     const FSEventStreamEventId ids[]) {
  size_t i;
  char** names = paths;
  (void) stream;
  (void) info;
  (void) flags;
  pthread_mutex_lock(&mutex);
  for (i = 0; i < n; i++)
    if (strcmp(names[i], expected) == 0 && ids[i] > floor_id) {
      observed = ids[i];
      pthread_cond_signal(&cond);
    }
  pthread_mutex_unlock(&mutex);
}

void uv__test_fsevents_observer_start(const char* root) {
  CFStringRef path;
  CFArrayRef paths;
  FSEventStreamContext context = {0};
  path = CFStringCreateWithFileSystemRepresentation(NULL, root);
  ASSERT(path != NULL);
  paths = CFArrayCreate(NULL, (const void**) &path, 1, &kCFTypeArrayCallBacks);
  ASSERT_NOT_NULL(paths);
  observer = FSEventStreamCreate(NULL, event_cb, &context, paths,
      kFSEventStreamEventIdSinceNow, 0.001,
      kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
  ASSERT(observer != NULL);
  queue = dispatch_queue_create("libuv.test.fsevents-observer", NULL);
  ASSERT_NOT_NULL(queue);
  FSEventStreamSetDispatchQueue(observer, queue);
  ASSERT(FSEventStreamStart(observer));
  CFRelease(paths);
  CFRelease(path);
}

static uint64_t mutate(const char* path, int remove) {
  struct timespec until;
  int fd;
  int rc = 0;
  uint64_t id;
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += 3;
  pthread_mutex_lock(&mutex);
  ASSERT(strlen(path) < sizeof(expected));
  strcpy(expected, path);
  observed = 0;
  floor_id = FSEventsGetCurrentEventId();
  if (remove) {
    ASSERT(unlink(path) == 0);
  } else {
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    ASSERT(fd >= 0);
    ASSERT(write(fd, "event", 5) == 5);
    ASSERT(fsync(fd) == 0);
    ASSERT(close(fd) == 0);
  }
  while (observed == 0 && rc == 0)
    rc = pthread_cond_timedwait(&cond, &mutex, &until);
  id = observed;
  pthread_mutex_unlock(&mutex);
  fprintf(stderr, "observer path=%s id=%llu wait=%d\n", path,
          (unsigned long long) id, rc);
  ASSERT(id != 0);
  return id;
}

uint64_t uv__test_fsevents_observer_mutate(const char* path) {
  return mutate(path, 0);
}

static void drained(void* arg) { (void) arg; }
void uv__test_fsevents_observer_stop(void) {
  FSEventStreamStop(observer);
  FSEventStreamInvalidate(observer);
  dispatch_sync_f(queue, NULL, drained);
  FSEventStreamRelease(observer);
  dispatch_release(queue);
}

/* Order directory-test setup before watch admission. These real native
 * acknowledgments replace neither application delivery nor its deadline. */
void uv__test_fsevents_fixture_ready(const char* root) {
  char marker[4096];
  char* path;
  int fd;
  int n;

  path = realpath(root, NULL);
  ASSERT_NOT_NULL(path);
  n = snprintf(marker, sizeof(marker), "%s/.libuv-ready-XXXXXX", path);
  ASSERT(n > 0 && (size_t) n < sizeof(marker));
  uv__test_fsevents_observer_start(path);
  fd = mkstemp(marker);
  ASSERT(fd >= 0);
  ASSERT(close(fd) == 0);
  mutate(marker, 0);
  mutate(marker, 1);
  uv__test_fsevents_observer_stop();
  free(path);
}

#endif
