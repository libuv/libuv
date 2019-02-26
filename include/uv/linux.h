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

#ifndef UV_LINUX_H
#define UV_LINUX_H

#define UV_LINUX_MAX_EVENTS_TO_LISTEN 1024
/* Best repeat count for poll in case when we obtain maximal number of events from epoll
 * Benchmarks suggest this gives the best throughput. */
#define UV_LINUX_EVENT_REPEAT_POLL_COUNT 48;

struct uv__removed_event_s;

typedef struct uv__removed_events_s {                                                                    \
  struct uv__removed_event_s *rbh_root; /*RB tree entry*/
  struct uv__removed_event_s *memory; /* predefined memory */
  int used_count; /*used count of entries in memory above*/
} uv__removed_events_t;

#define UV_PLATFORM_LOOP_FIELDS                                               \
  uv__io_t inotify_read_watcher;                                              \
  void* inotify_watchers;                                                     \
  int inotify_fd;                                                             \
  /*next field only available inside uv__io_poll e.g. in poll callbacks*/     \
  struct uv__removed_events_s removed_events;                                 \

#define UV_PLATFORM_FS_EVENT_FIELDS                                           \
  void* watchers[2];                                                          \
  int wd;                                                                     \

#endif /* UV_LINUX_H */
