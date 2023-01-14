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

/* We lean on the fact that POLL{IN,OUT,ERR,HUP} correspond with their
 * EPOLL* counterparts.  We use the POLL* variants in this file because that
 * is what libuv uses elsewhere.
 */

#include "uv.h"
#include "internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <fcntl.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__arm__)
# if defined(__thumb__) || defined(__ARM_EABI__)
#  define UV_SYSCALL_BASE 0
# else
#  define UV_SYSCALL_BASE 0x900000
# endif
#endif /* __arm__ */

#ifndef __NR_copy_file_range
# if defined(__x86_64__)
#  define __NR_copy_file_range 326
# elif defined(__i386__)
#  define __NR_copy_file_range 377
# elif defined(__s390__)
#  define __NR_copy_file_range 375
# elif defined(__arm__)
#  define __NR_copy_file_range (UV_SYSCALL_BASE + 391)
# elif defined(__aarch64__)
#  define __NR_copy_file_range 285
# elif defined(__powerpc__)
#  define __NR_copy_file_range 379
# elif defined(__arc__)
#  define __NR_copy_file_range 285
# endif
#endif /* __NR_copy_file_range */

#ifndef __NR_statx
# if defined(__x86_64__)
#  define __NR_statx 332
# elif defined(__i386__)
#  define __NR_statx 383
# elif defined(__aarch64__)
#  define __NR_statx 397
# elif defined(__arm__)
#  define __NR_statx (UV_SYSCALL_BASE + 397)
# elif defined(__ppc__)
#  define __NR_statx 383
# elif defined(__s390__)
#  define __NR_statx 379
# endif
#endif /* __NR_statx */

#ifndef __NR_getrandom
# if defined(__x86_64__)
#  define __NR_getrandom 318
# elif defined(__i386__)
#  define __NR_getrandom 355
# elif defined(__aarch64__)
#  define __NR_getrandom 384
# elif defined(__arm__)
#  define __NR_getrandom (UV_SYSCALL_BASE + 384)
# elif defined(__ppc__)
#  define __NR_getrandom 359
# elif defined(__s390__)
#  define __NR_getrandom 349
# endif
#endif /* __NR_getrandom */

#define HAVE_IFADDRS_H 1

# if defined(__ANDROID_API__) && __ANDROID_API__ < 24
# undef HAVE_IFADDRS_H
#endif

#ifdef __UCLIBC__
# if __UCLIBC_MAJOR__ < 0 && __UCLIBC_MINOR__ < 9 && __UCLIBC_SUBLEVEL__ < 32
#  undef HAVE_IFADDRS_H
# endif
#endif

#ifdef HAVE_IFADDRS_H
# include <ifaddrs.h>
# include <sys/socket.h>
# include <net/ethernet.h>
# include <netpacket/packet.h>
#endif /* HAVE_IFADDRS_H */

#define CAST(p) ((struct watcher_root*)(p))

struct watcher_list {
  RB_ENTRY(watcher_list) entry;
  QUEUE watchers;
  int iterating;
  char* path;
  int wd;
};

struct watcher_root {
  struct watcher_list* rbh_root;
};

static void uv__inotify_read(uv_loop_t* loop,
                             uv__io_t* w,
                             unsigned int revents);
static int compare_watchers(const struct watcher_list* a,
                            const struct watcher_list* b);
static void maybe_free_watcher_list(struct watcher_list* w,
                                    uv_loop_t* loop);

RB_GENERATE_STATIC(watcher_root, watcher_list, entry, compare_watchers)


ssize_t
uv__fs_copy_file_range(int fd_in,
                       off_t* off_in,
                       int fd_out,
                       off_t* off_out,
                       size_t len,
                       unsigned int flags)
{
#ifdef __NR_copy_file_range
  return syscall(__NR_copy_file_range,
                 fd_in,
                 off_in,
                 fd_out,
                 off_out,
                 len,
                 flags);
#else
  return errno = ENOSYS, -1;
#endif
}


int uv__statx(int dirfd,
              const char* path,
              int flags,
              unsigned int mask,
              struct uv__statx* statxbuf) {
#if !defined(__NR_statx) || defined(__ANDROID_API__) && __ANDROID_API__ < 30
  return errno = ENOSYS, -1;
#else
  int rc;

  rc = syscall(__NR_statx, dirfd, path, flags, mask, statxbuf);
  if (rc >= 0)
    uv__msan_unpoison(statxbuf, sizeof(*statxbuf));

  return rc;
#endif
}


ssize_t uv__getrandom(void* buf, size_t buflen, unsigned flags) {
#if !defined(__NR_getrandom) || defined(__ANDROID_API__) && __ANDROID_API__ < 28
  return errno = ENOSYS, -1;
#else
  ssize_t rc;

  rc = syscall(__NR_getrandom, buf, buflen, flags);
  if (rc >= 0)
    uv__msan_unpoison(buf, buflen);

  return rc;
#endif
}


int uv__platform_loop_init(uv_loop_t* loop) {
  loop->inotify_watchers = NULL;
  loop->inotify_fd = -1;
  loop->backend_fd = epoll_create1(O_CLOEXEC);

  if (loop->backend_fd == -1)
    return UV__ERR(errno);

  return 0;
}


int uv__io_fork(uv_loop_t* loop) {
  int err;
  void* old_watchers;

  old_watchers = loop->inotify_watchers;

  uv__close(loop->backend_fd);
  loop->backend_fd = -1;
  uv__platform_loop_delete(loop);

  err = uv__platform_loop_init(loop);
  if (err)
    return err;

  return uv__inotify_fork(loop, old_watchers);
}


void uv__platform_loop_delete(uv_loop_t* loop) {
  if (loop->inotify_fd == -1) return;
  uv__io_stop(loop, &loop->inotify_read_watcher, POLLIN);
  uv__close(loop->inotify_fd);
  loop->inotify_fd = -1;
}


void uv__platform_invalidate_fd(uv_loop_t* loop, int fd) {
  struct epoll_event* events;
  struct epoll_event dummy;
  uintptr_t i;
  uintptr_t nfds;

  assert(loop->watchers != NULL);
  assert(fd >= 0);

  events = (struct epoll_event*) loop->watchers[loop->nwatchers];
  nfds = (uintptr_t) loop->watchers[loop->nwatchers + 1];
  if (events != NULL)
    /* Invalidate events with same file descriptor */
    for (i = 0; i < nfds; i++)
      if (events[i].data.fd == fd)
        events[i].data.fd = -1;

  /* Remove the file descriptor from the epoll.
   * This avoids a problem where the same file description remains open
   * in another process, causing repeated junk epoll events.
   *
   * We pass in a dummy epoll_event, to work around a bug in old kernels.
   */
  if (loop->backend_fd >= 0) {
    /* Work around a bug in kernels 3.10 to 3.19 where passing a struct that
     * has the EPOLLWAKEUP flag set generates spurious audit syslog warnings.
     */
    memset(&dummy, 0, sizeof(dummy));
    epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, &dummy);
  }
}


int uv__io_check_fd(uv_loop_t* loop, int fd) {
  struct epoll_event e;
  int rc;

  memset(&e, 0, sizeof(e));
  e.events = POLLIN;
  e.data.fd = -1;

  rc = 0;
  if (epoll_ctl(loop->backend_fd, EPOLL_CTL_ADD, fd, &e))
    if (errno != EEXIST)
      rc = UV__ERR(errno);

  if (rc == 0)
    if (epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, &e))
      abort();

  return rc;
}


void uv__io_poll(uv_loop_t* loop, int timeout) {
  /* A bug in kernels < 2.6.37 makes timeouts larger than ~30 minutes
   * effectively infinite on 32 bits architectures.  To avoid blocking
   * indefinitely, we cap the timeout and poll again if necessary.
   *
   * Note that "30 minutes" is a simplification because it depends on
   * the value of CONFIG_HZ.  The magic constant assumes CONFIG_HZ=1200,
   * that being the largest value I have seen in the wild (and only once.)
   */
  static const int max_safe_timeout = 1789569;
  static int no_epoll_pwait_cached;
  static int no_epoll_wait_cached;
  int no_epoll_pwait;
  int no_epoll_wait;
  struct epoll_event events[1024];
  struct epoll_event* pe;
  struct epoll_event e;
  int real_timeout;
  QUEUE* q;
  uv__io_t* w;
  sigset_t sigset;
  uint64_t sigmask;
  uint64_t base;
  int have_signals;
  int nevents;
  int count;
  int nfds;
  int fd;
  int op;
  int i;
  int user_timeout;
  int reset_timeout;

  if (loop->nfds == 0) {
    assert(QUEUE_EMPTY(&loop->watcher_queue));
    return;
  }

  memset(&e, 0, sizeof(e));

  while (!QUEUE_EMPTY(&loop->watcher_queue)) {
    q = QUEUE_HEAD(&loop->watcher_queue);
    QUEUE_REMOVE(q);
    QUEUE_INIT(q);

    w = QUEUE_DATA(q, uv__io_t, watcher_queue);
    assert(w->pevents != 0);
    assert(w->fd >= 0);
    assert(w->fd < (int) loop->nwatchers);

    e.events = w->pevents;
    e.data.fd = w->fd;

    if (w->events == 0)
      op = EPOLL_CTL_ADD;
    else
      op = EPOLL_CTL_MOD;

    /* XXX Future optimization: do EPOLL_CTL_MOD lazily if we stop watching
     * events, skip the syscall and squelch the events after epoll_wait().
     */
    if (epoll_ctl(loop->backend_fd, op, w->fd, &e)) {
      if (errno != EEXIST)
        abort();

      assert(op == EPOLL_CTL_ADD);

      /* We've reactivated a file descriptor that's been watched before. */
      if (epoll_ctl(loop->backend_fd, EPOLL_CTL_MOD, w->fd, &e))
        abort();
    }

    w->events = w->pevents;
  }

  sigmask = 0;
  if (loop->flags & UV_LOOP_BLOCK_SIGPROF) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPROF);
    sigmask |= 1 << (SIGPROF - 1);
  }

  assert(timeout >= -1);
  base = loop->time;
  count = 48; /* Benchmarks suggest this gives the best throughput. */
  real_timeout = timeout;

  if (uv__get_internal_fields(loop)->flags & UV_METRICS_IDLE_TIME) {
    reset_timeout = 1;
    user_timeout = timeout;
    timeout = 0;
  } else {
    reset_timeout = 0;
    user_timeout = 0;
  }

  /* You could argue there is a dependency between these two but
   * ultimately we don't care about their ordering with respect
   * to one another. Worst case, we make a few system calls that
   * could have been avoided because another thread already knows
   * they fail with ENOSYS. Hardly the end of the world.
   */
  no_epoll_pwait = uv__load_relaxed(&no_epoll_pwait_cached);
  no_epoll_wait = uv__load_relaxed(&no_epoll_wait_cached);

  for (;;) {
    /* Only need to set the provider_entry_time if timeout != 0. The function
     * will return early if the loop isn't configured with UV_METRICS_IDLE_TIME.
     */
    if (timeout != 0)
      uv__metrics_set_provider_entry_time(loop);

    /* See the comment for max_safe_timeout for an explanation of why
     * this is necessary.  Executive summary: kernel bug workaround.
     */
    if (sizeof(int32_t) == sizeof(long) && timeout >= max_safe_timeout)
      timeout = max_safe_timeout;

    if (sigmask != 0 && no_epoll_pwait != 0)
      if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        abort();

    if (no_epoll_wait != 0 || (sigmask != 0 && no_epoll_pwait == 0)) {
      nfds = epoll_pwait(loop->backend_fd,
                         events,
                         ARRAY_SIZE(events),
                         timeout,
                         &sigset);
      if (nfds == -1 && errno == ENOSYS) {
        uv__store_relaxed(&no_epoll_pwait_cached, 1);
        no_epoll_pwait = 1;
      }
    } else {
      nfds = epoll_wait(loop->backend_fd,
                        events,
                        ARRAY_SIZE(events),
                        timeout);
      if (nfds == -1 && errno == ENOSYS) {
        uv__store_relaxed(&no_epoll_wait_cached, 1);
        no_epoll_wait = 1;
      }
    }

    if (sigmask != 0 && no_epoll_pwait != 0)
      if (pthread_sigmask(SIG_UNBLOCK, &sigset, NULL))
        abort();

    /* Update loop->time unconditionally. It's tempting to skip the update when
     * timeout == 0 (i.e. non-blocking poll) but there is no guarantee that the
     * operating system didn't reschedule our process while in the syscall.
     */
    SAVE_ERRNO(uv__update_time(loop));

    if (nfds == 0) {
      assert(timeout != -1);

      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      }

      if (timeout == -1)
        continue;

      if (timeout == 0)
        return;

      /* We may have been inside the system call for longer than |timeout|
       * milliseconds so we need to update the timestamp to avoid drift.
       */
      goto update_timeout;
    }

    if (nfds == -1) {
      if (errno == ENOSYS) {
        /* epoll_wait() or epoll_pwait() failed, try the other system call. */
        assert(no_epoll_wait == 0 || no_epoll_pwait == 0);
        continue;
      }

      if (errno != EINTR)
        abort();

      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      }

      if (timeout == -1)
        continue;

      if (timeout == 0)
        return;

      /* Interrupted by a signal. Update timeout and poll again. */
      goto update_timeout;
    }

    have_signals = 0;
    nevents = 0;

    {
      /* Squelch a -Waddress-of-packed-member warning with gcc >= 9. */
      union {
        struct epoll_event* events;
        uv__io_t* watchers;
      } x;

      x.events = events;
      assert(loop->watchers != NULL);
      loop->watchers[loop->nwatchers] = x.watchers;
      loop->watchers[loop->nwatchers + 1] = (void*) (uintptr_t) nfds;
    }

    for (i = 0; i < nfds; i++) {
      pe = events + i;
      fd = pe->data.fd;

      /* Skip invalidated events, see uv__platform_invalidate_fd */
      if (fd == -1)
        continue;

      assert(fd >= 0);
      assert((unsigned) fd < loop->nwatchers);

      w = loop->watchers[fd];

      if (w == NULL) {
        /* File descriptor that we've stopped watching, disarm it.
         *
         * Ignore all errors because we may be racing with another thread
         * when the file descriptor is closed.
         */
        epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, pe);
        continue;
      }

      /* Give users only events they're interested in. Prevents spurious
       * callbacks when previous callback invocation in this loop has stopped
       * the current watcher. Also, filters out events that users has not
       * requested us to watch.
       */
      pe->events &= w->pevents | POLLERR | POLLHUP;

      /* Work around an epoll quirk where it sometimes reports just the
       * EPOLLERR or EPOLLHUP event.  In order to force the event loop to
       * move forward, we merge in the read/write events that the watcher
       * is interested in; uv__read() and uv__write() will then deal with
       * the error or hangup in the usual fashion.
       *
       * Note to self: happens when epoll reports EPOLLIN|EPOLLHUP, the user
       * reads the available data, calls uv_read_stop(), then sometime later
       * calls uv_read_start() again.  By then, libuv has forgotten about the
       * hangup and the kernel won't report EPOLLIN again because there's
       * nothing left to read.  If anything, libuv is to blame here.  The
       * current hack is just a quick bandaid; to properly fix it, libuv
       * needs to remember the error/hangup event.  We should get that for
       * free when we switch over to edge-triggered I/O.
       */
      if (pe->events == POLLERR || pe->events == POLLHUP)
        pe->events |=
          w->pevents & (POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI);

      if (pe->events != 0) {
        /* Run signal watchers last.  This also affects child process watchers
         * because those are implemented in terms of signal watchers.
         */
        if (w == &loop->signal_io_watcher) {
          have_signals = 1;
        } else {
          uv__metrics_update_idle_time(loop);
          w->cb(loop, w, pe->events);
        }

        nevents++;
      }
    }

    uv__metrics_inc_events(loop, nevents);
    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
      uv__metrics_inc_events_waiting(loop, nevents);
    }

    if (have_signals != 0) {
      uv__metrics_update_idle_time(loop);
      loop->signal_io_watcher.cb(loop, &loop->signal_io_watcher, POLLIN);
    }

    loop->watchers[loop->nwatchers] = NULL;
    loop->watchers[loop->nwatchers + 1] = NULL;

    if (have_signals != 0)
      return;  /* Event loop should cycle now so don't poll again. */

    if (nevents != 0) {
      if (nfds == ARRAY_SIZE(events) && --count != 0) {
        /* Poll for more events but don't block this time. */
        timeout = 0;
        continue;
      }
      return;
    }

    if (timeout == 0)
      return;

    if (timeout == -1)
      continue;

update_timeout:
    assert(timeout > 0);

    real_timeout -= (loop->time - base);
    if (real_timeout <= 0)
      return;

    timeout = real_timeout;
  }
}

uint64_t uv__hrtime(uv_clocktype_t type) {
  static clock_t fast_clock_id = -1;
  struct timespec t;
  clock_t clock_id;

  /* Prefer CLOCK_MONOTONIC_COARSE if available but only when it has
   * millisecond granularity or better.  CLOCK_MONOTONIC_COARSE is
   * serviced entirely from the vDSO, whereas CLOCK_MONOTONIC may
   * decide to make a costly system call.
   */
  /* TODO(bnoordhuis) Use CLOCK_MONOTONIC_COARSE for UV_CLOCK_PRECISE
   * when it has microsecond granularity or better (unlikely).
   */
  clock_id = CLOCK_MONOTONIC;
  if (type != UV_CLOCK_FAST)
    goto done;

  clock_id = uv__load_relaxed(&fast_clock_id);
  if (clock_id != -1)
    goto done;

  clock_id = CLOCK_MONOTONIC;
  if (0 == clock_getres(CLOCK_MONOTONIC_COARSE, &t))
    if (t.tv_nsec <= 1 * 1000 * 1000)
      clock_id = CLOCK_MONOTONIC_COARSE;

  uv__store_relaxed(&fast_clock_id, clock_id);

done:

  if (clock_gettime(clock_id, &t))
    return 0;  /* Not really possible. */

  return t.tv_sec * (uint64_t) 1e9 + t.tv_nsec;
}


int uv_resident_set_memory(size_t* rss) {
  char buf[1024];
  const char* s;
  ssize_t n;
  long val;
  int fd;
  int i;

  do
    fd = open("/proc/self/stat", O_RDONLY);
  while (fd == -1 && errno == EINTR);

  if (fd == -1)
    return UV__ERR(errno);

  do
    n = read(fd, buf, sizeof(buf) - 1);
  while (n == -1 && errno == EINTR);

  uv__close(fd);
  if (n == -1)
    return UV__ERR(errno);
  buf[n] = '\0';

  s = strchr(buf, ' ');
  if (s == NULL)
    goto err;

  s += 1;
  if (*s != '(')
    goto err;

  s = strchr(s, ')');
  if (s == NULL)
    goto err;

  for (i = 1; i <= 22; i++) {
    s = strchr(s + 1, ' ');
    if (s == NULL)
      goto err;
  }

  errno = 0;
  val = strtol(s, NULL, 10);
  if (errno != 0)
    goto err;
  if (val < 0)
    goto err;

  *rss = val * getpagesize();
  return 0;

err:
  return UV_EINVAL;
}

int uv_uptime(double* uptime) {
  struct timespec now;
  char buf[128];

  /* Consult /proc/uptime when present (common case), or fall back to
   * clock_gettime. Why not always clock_gettime? It doesn't always return the
   * right result under OpenVZ and possibly other containerized environments.
   */
  if (0 == uv__slurp("/proc/uptime", buf, sizeof(buf)))
    if (1 == sscanf(buf, "%lf", uptime))
      return 0;

  if (clock_gettime(CLOCK_BOOTTIME, &now))
    return UV__ERR(errno);

  *uptime = now.tv_sec;
  return 0;
}


int uv_cpu_info(uv_cpu_info_t** ci, int* count) {
#if defined(__PPC__)
  static const char model_marker[] = "cpu\t\t: ";
#elif defined(__arm__)
  static const char model_marker[] = "Processor\t: ";
#elif defined(__aarch64__)
  static const char model_marker[] = "CPU part\t: ";
#elif defined(__mips__)
  static const char model_marker[] = "cpu model\t\t: ";
#else
  static const char model_marker[] = "model name\t: ";
#endif
  static const char parts[] =
#ifdef __aarch64__
    "0x811\nARM810\n"       "0x920\nARM920\n"      "0x922\nARM922\n"
    "0x926\nARM926\n"       "0x940\nARM940\n"      "0x946\nARM946\n"
    "0x966\nARM966\n"       "0xa20\nARM1020\n"      "0xa22\nARM1022\n"
    "0xa26\nARM1026\n"      "0xb02\nARM11 MPCore\n" "0xb36\nARM1136\n"
    "0xb56\nARM1156\n"      "0xb76\nARM1176\n"      "0xc05\nCortex-A5\n"
    "0xc07\nCortex-A7\n"    "0xc08\nCortex-A8\n"    "0xc09\nCortex-A9\n"
    "0xc0d\nCortex-A17\n"   /* Originally A12 */
    "0xc0f\nCortex-A15\n"   "0xc0e\nCortex-A17\n"   "0xc14\nCortex-R4\n"
    "0xc15\nCortex-R5\n"    "0xc17\nCortex-R7\n"    "0xc18\nCortex-R8\n"
    "0xc20\nCortex-M0\n"    "0xc21\nCortex-M1\n"    "0xc23\nCortex-M3\n"
    "0xc24\nCortex-M4\n"    "0xc27\nCortex-M7\n"    "0xc60\nCortex-M0+\n"
    "0xd01\nCortex-A32\n"   "0xd03\nCortex-A53\n"   "0xd04\nCortex-A35\n"
    "0xd05\nCortex-A55\n"   "0xd06\nCortex-A65\n"   "0xd07\nCortex-A57\n"
    "0xd08\nCortex-A72\n"   "0xd09\nCortex-A73\n"   "0xd0a\nCortex-A75\n"
    "0xd0b\nCortex-A76\n"   "0xd0c\nNeoverse-N1\n"  "0xd0d\nCortex-A77\n"
    "0xd0e\nCortex-A76AE\n" "0xd13\nCortex-R52\n"   "0xd20\nCortex-M23\n"
    "0xd21\nCortex-M33\n"   "0xd41\nCortex-A78\n"   "0xd42\nCortex-A78AE\n"
    "0xd4a\nNeoverse-E1\n"  "0xd4b\nCortex-A78C\n"
#endif
    "";
  struct cpu {
    unsigned long long freq, user, nice, sys, idle, irq;
    unsigned model;
  };
  FILE* fp;
  char* p;
  int found;
  int n;
  unsigned i;
  unsigned cpu;
  unsigned maxcpu;
  unsigned size;
  unsigned long long skip;
  struct cpu (*cpus)[8192];  /* Kernel maximum. */
  struct cpu* c;
  struct cpu t;
  char (*model)[64];
  unsigned char bitmap[ARRAY_SIZE(*cpus) / 8];
  /* Assumption: even big.LITTLE systems will have only a handful
   * of different CPU models. Most systems will just have one.
   */
  char models[8][64];
  char buf[1024];

  memset(bitmap, 0, sizeof(bitmap));
  memset(models, 0, sizeof(models));
  snprintf(*models, sizeof(*models), "unknown");
  maxcpu = 0;

  cpus = uv__calloc(ARRAY_SIZE(*cpus), sizeof(**cpus));
  if (cpus == NULL)
    return UV_ENOMEM;

  fp = uv__open_file("/proc/stat");
  if (fp == NULL) {
    uv__free(cpus);
    return UV__ERR(errno);
  }

  fgets(buf, sizeof(buf), fp);  /* Skip first line. */

  for (;;) {
    memset(&t, 0, sizeof(t));

    n = fscanf(fp, "cpu%u %llu %llu %llu %llu %llu %llu",
               &cpu, &t.user, &t.nice, &t.sys, &t.idle, &skip, &t.irq);

    if (n != 7)
      break;

    fgets(buf, sizeof(buf), fp);  /* Skip rest of line. */

    if (cpu >= ARRAY_SIZE(*cpus))
      continue;

    (*cpus)[cpu] = t;

    bitmap[cpu >> 3] |= 1 << (cpu & 7);

    if (cpu >= maxcpu)
      maxcpu = cpu + 1;
  }

  fclose(fp);

  fp = uv__open_file("/proc/cpuinfo");
  if (fp == NULL)
    goto nocpuinfo;

  for (;;) {
    if (1 != fscanf(fp, "processor\t: %u\n", &cpu))
      break;  /* Parse error. */

    found = 0;
    while (!found && fgets(buf, sizeof(buf), fp))
      found = !strncmp(buf, model_marker, sizeof(model_marker) - 1);

    if (!found)
      goto next;

    p = buf + sizeof(model_marker) - 1;
    n = (int) strcspn(p, "\n");

    /* arm64: translate CPU part code to model name. */
    if (*parts) {
      p = memmem(parts, sizeof(parts) - 1, p, n + 1);
      if (p == NULL)
        p = "unknown";
      else
        p += n + 1;
      n = (int) strcspn(p, "\n");
    }

    found = 0;
    for (model = models; !found && model < ARRAY_END(models); model++)
      found = !strncmp(p, *model, strlen(*model));

    if (!found)
      goto next;

    if (**model == '\0')
      snprintf(*model, sizeof(*model), "%.*s", n, p);

    if (cpu < maxcpu)
      (*cpus)[cpu].model = model - models;

next:
    while (fgets(buf, sizeof(buf), fp))
      if (*buf == '\n')
        break;
  }

  fclose(fp);
  fp = NULL;

nocpuinfo:

  n = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    if (!(bitmap[cpu >> 3] & (1 << (cpu & 7))))
      continue;

    n++;
    snprintf(buf, sizeof(buf),
             "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);

    fp = uv__open_file(buf);
    if (fp == NULL)
      continue;

    fscanf(fp, "%llu", &(*cpus)[cpu].freq);
    fclose(fp);
    fp = NULL;
  }

  size = n * sizeof(**ci) + sizeof(models);
  *ci = uv__malloc(size);
  *count = 0;

  if (*ci == NULL) {
    uv__free(cpus);
    return UV_ENOMEM;
  }

  *count = n;
  p = memcpy(*ci + n, models, sizeof(models));

  i = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    if (!(bitmap[cpu >> 3] & (1 << (cpu & 7))))
      continue;

    c = *cpus + cpu;

    (*ci)[i++] = (uv_cpu_info_t) {
      .model     = p + c->model * sizeof(*model),
      .speed     = c->freq / 1000,
      /* Note: sysconf(_SC_CLK_TCK) is fixed at 100 Hz,
       * therefore the multiplier is always 1000/100 = 10.
       */
      .cpu_times = (struct uv_cpu_times_s) {
        .user = 10 * c->user,
        .nice = 10 * c->nice,
        .sys  = 10 * c->sys,
        .idle = 10 * c->idle,
        .irq  = 10 * c->irq,
      },
    };
  }

  uv__free(cpus);

  return 0;
}


#ifdef HAVE_IFADDRS_H
static int uv__ifaddr_exclude(struct ifaddrs *ent, int exclude_type) {
  if (!((ent->ifa_flags & IFF_UP) && (ent->ifa_flags & IFF_RUNNING)))
    return 1;
  if (ent->ifa_addr == NULL)
    return 1;
  /*
   * On Linux getifaddrs returns information related to the raw underlying
   * devices. We're not interested in this information yet.
   */
  if (ent->ifa_addr->sa_family == PF_PACKET)
    return exclude_type;
  return !exclude_type;
}
#endif

int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
#ifndef HAVE_IFADDRS_H
  *count = 0;
  *addresses = NULL;
  return UV_ENOSYS;
#else
  struct ifaddrs *addrs, *ent;
  uv_interface_address_t* address;
  int i;
  struct sockaddr_ll *sll;

  *count = 0;
  *addresses = NULL;

  if (getifaddrs(&addrs))
    return UV__ERR(errno);

  /* Count the number of interfaces */
  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;

    (*count)++;
  }

  if (*count == 0) {
    freeifaddrs(addrs);
    return 0;
  }

  /* Make sure the memory is initiallized to zero using calloc() */
  *addresses = uv__calloc(*count, sizeof(**addresses));
  if (!(*addresses)) {
    freeifaddrs(addrs);
    return UV_ENOMEM;
  }

  address = *addresses;

  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;

    address->name = uv__strdup(ent->ifa_name);

    if (ent->ifa_addr->sa_family == AF_INET6) {
      address->address.address6 = *((struct sockaddr_in6*) ent->ifa_addr);
    } else {
      address->address.address4 = *((struct sockaddr_in*) ent->ifa_addr);
    }

    if (ent->ifa_netmask->sa_family == AF_INET6) {
      address->netmask.netmask6 = *((struct sockaddr_in6*) ent->ifa_netmask);
    } else {
      address->netmask.netmask4 = *((struct sockaddr_in*) ent->ifa_netmask);
    }

    address->is_internal = !!(ent->ifa_flags & IFF_LOOPBACK);

    address++;
  }

  /* Fill in physical addresses for each interface */
  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFPHYS))
      continue;

    address = *addresses;

    for (i = 0; i < (*count); i++) {
      size_t namelen = strlen(ent->ifa_name);
      /* Alias interface share the same physical address */
      if (strncmp(address->name, ent->ifa_name, namelen) == 0 &&
          (address->name[namelen] == 0 || address->name[namelen] == ':')) {
        sll = (struct sockaddr_ll*)ent->ifa_addr;
        memcpy(address->phys_addr, sll->sll_addr, sizeof(address->phys_addr));
      }
      address++;
    }
  }

  freeifaddrs(addrs);

  return 0;
#endif
}


void uv_free_interface_addresses(uv_interface_address_t* addresses,
  int count) {
  int i;

  for (i = 0; i < count; i++) {
    uv__free(addresses[i].name);
  }

  uv__free(addresses);
}


void uv__set_process_title(const char* title) {
#if defined(PR_SET_NAME)
  prctl(PR_SET_NAME, title);  /* Only copies first 16 characters. */
#endif
}


static uint64_t uv__read_proc_meminfo(const char* what) {
  uint64_t rc;
  char* p;
  char buf[4096];  /* Large enough to hold all of /proc/meminfo. */

  if (uv__slurp("/proc/meminfo", buf, sizeof(buf)))
    return 0;

  p = strstr(buf, what);

  if (p == NULL)
    return 0;

  p += strlen(what);

  rc = 0;
  sscanf(p, "%" PRIu64 " kB", &rc);

  return rc * 1024;
}


uint64_t uv_get_free_memory(void) {
  struct sysinfo info;
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemAvailable:");

  if (rc != 0)
    return rc;

  if (0 == sysinfo(&info))
    return (uint64_t) info.freeram * info.mem_unit;

  return 0;
}


uint64_t uv_get_total_memory(void) {
  struct sysinfo info;
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemTotal:");

  if (rc != 0)
    return rc;

  if (0 == sysinfo(&info))
    return (uint64_t) info.totalram * info.mem_unit;

  return 0;
}


static uint64_t uv__read_uint64(const char* filename) {
  char buf[32];  /* Large enough to hold an encoded uint64_t. */
  uint64_t rc;

  rc = 0;
  if (0 == uv__slurp(filename, buf, sizeof(buf)))
    if (1 != sscanf(buf, "%" PRIu64, &rc))
      if (0 == strcmp(buf, "max\n"))
        rc = ~0ull;

  return rc;
}


/* Given a buffer with the contents of a cgroup1 /proc/self/cgroups,
 * finds the location and length of the memory controller mount path.
 * This disregards the leading / for easy concatenation of paths.
 * Returns NULL if the memory controller wasn't found. */
static char* uv__cgroup1_find_memory_controller(char buf[static 1024],
                                                int* n) {
  char* p;

  /* Seek to the memory controller line. */
  p = strchr(buf, ':');
  while (p != NULL && strncmp(p, ":memory:", 8)) {
    p = strchr(p, '\n');
    if (p != NULL)
      p = strchr(p, ':');
  }

  if (p != NULL) {
    /* Determine the length of the mount path. */
    p = p + strlen(":memory:/");
    *n = (int) strcspn(p, "\n");
  }

  return p;
}

static void uv__get_cgroup1_memory_limits(char buf[static 1024], uint64_t* high,
                                          uint64_t* max) {
  char filename[4097];
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = uv__cgroup1_find_memory_controller(buf, &n);
  if (p != NULL) {
    snprintf(filename, sizeof(filename),
             "/sys/fs/cgroup/memory/%.*s/memory.soft_limit_in_bytes", n, p);
    *high = uv__read_uint64(filename);

    snprintf(filename, sizeof(filename),
             "/sys/fs/cgroup/memory/%.*s/memory.limit_in_bytes", n, p);
    *max = uv__read_uint64(filename);

    /* If the controller wasn't mounted, the reads above will have failed,
     * as indicated by uv__read_uint64 returning 0.
     */
     if (*high != 0 && *max != 0)
       return;
  }

  /* Fall back to the limits of the global memory controller. */
  *high = uv__read_uint64("/sys/fs/cgroup/memory/memory.soft_limit_in_bytes");
  *max = uv__read_uint64("/sys/fs/cgroup/memory/memory.limit_in_bytes");
}

static void uv__get_cgroup2_memory_limits(char buf[static 1024], uint64_t* high,
                                          uint64_t* max) {
  char filename[4097];
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = buf + strlen("0::/");
  n = (int) strcspn(p, "\n");

  /* Read the memory limits of the controller. */
  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%.*s/memory.max", n, p);
  *max = uv__read_uint64(filename);
  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%.*s/memory.high", n, p);
  *high = uv__read_uint64(filename);
}

static uint64_t uv__get_cgroup_constrained_memory(char buf[static 1024]) {
  uint64_t high;
  uint64_t max;

  /* In the case of cgroupv2, we'll only have a single entry. */
  if (strncmp(buf, "0::/", 4))
    uv__get_cgroup1_memory_limits(buf, &high, &max);
  else
    uv__get_cgroup2_memory_limits(buf, &high, &max);

  if (high == 0 || max == 0)
    return 0;

  return high < max ? high : max;
}

uint64_t uv_get_constrained_memory(void) {
  char buf[1024];

  if (uv__slurp("/proc/self/cgroup", buf, sizeof(buf)))
    return 0;

  return uv__get_cgroup_constrained_memory(buf);
}


static uint64_t uv__get_cgroup1_current_memory(char buf[static 1024]) {
  char filename[4097];
  uint64_t current;
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = uv__cgroup1_find_memory_controller(buf, &n);
  if (p != NULL) {
    snprintf(filename, sizeof(filename),
            "/sys/fs/cgroup/memory/%.*s/memory.usage_in_bytes", n, p);
    current = uv__read_uint64(filename);

    /* If the controller wasn't mounted, the reads above will have failed,
     * as indicated by uv__read_uint64 returning 0.
     */
    if (current != 0)
      return current;
  }

  /* Fall back to the usage of the global memory controller. */
  return uv__read_uint64("/sys/fs/cgroup/memory/memory.usage_in_bytes");
}

static uint64_t uv__get_cgroup2_current_memory(char buf[static 1024]) {
  char filename[4097];
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = buf + strlen("0::/");
  n = (int) strcspn(p, "\n");

  snprintf(filename, sizeof(filename),
           "/sys/fs/cgroup/%.*s/memory.current", n, p);
  return uv__read_uint64(filename);
}

uint64_t uv_get_available_memory(void) {
  char buf[1024];
  uint64_t constrained;
  uint64_t current;
  uint64_t total;

  if (uv__slurp("/proc/self/cgroup", buf, sizeof(buf)))
    return 0;

  constrained = uv__get_cgroup_constrained_memory(buf);
  if (constrained == 0)
    return uv_get_free_memory();

  total = uv_get_total_memory();
  if (constrained > total)
    return uv_get_free_memory();

  /* In the case of cgroupv2, we'll only have a single entry. */
  if (strncmp(buf, "0::/", 4))
    current = uv__get_cgroup1_current_memory(buf);
  else
    current = uv__get_cgroup2_current_memory(buf);

  /* memory usage can be higher than the limit (for short bursts of time) */
  if (constrained < current)
    return 0;

  return constrained - current;
}


void uv_loadavg(double avg[3]) {
  struct sysinfo info;
  char buf[128];  /* Large enough to hold all of /proc/loadavg. */

  if (0 == uv__slurp("/proc/loadavg", buf, sizeof(buf)))
    if (3 == sscanf(buf, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]))
      return;

  if (sysinfo(&info) < 0)
    return;

  avg[0] = (double) info.loads[0] / 65536.0;
  avg[1] = (double) info.loads[1] / 65536.0;
  avg[2] = (double) info.loads[2] / 65536.0;
}


static int compare_watchers(const struct watcher_list* a,
                            const struct watcher_list* b) {
  if (a->wd < b->wd) return -1;
  if (a->wd > b->wd) return 1;
  return 0;
}


static int init_inotify(uv_loop_t* loop) {
  int fd;

  if (loop->inotify_fd != -1)
    return 0;

  fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0)
    return UV__ERR(errno);

  loop->inotify_fd = fd;
  uv__io_init(&loop->inotify_read_watcher, uv__inotify_read, loop->inotify_fd);
  uv__io_start(loop, &loop->inotify_read_watcher, POLLIN);

  return 0;
}


int uv__inotify_fork(uv_loop_t* loop, void* old_watchers) {
  /* Open the inotify_fd, and re-arm all the inotify watchers. */
  int err;
  struct watcher_list* tmp_watcher_list_iter;
  struct watcher_list* watcher_list;
  struct watcher_list tmp_watcher_list;
  QUEUE queue;
  QUEUE* q;
  uv_fs_event_t* handle;
  char* tmp_path;

  if (old_watchers != NULL) {
    /* We must restore the old watcher list to be able to close items
     * out of it.
     */
    loop->inotify_watchers = old_watchers;

    QUEUE_INIT(&tmp_watcher_list.watchers);
    /* Note that the queue we use is shared with the start and stop()
     * functions, making QUEUE_FOREACH unsafe to use. So we use the
     * QUEUE_MOVE trick to safely iterate. Also don't free the watcher
     * list until we're done iterating. c.f. uv__inotify_read.
     */
    RB_FOREACH_SAFE(watcher_list, watcher_root,
                    CAST(&old_watchers), tmp_watcher_list_iter) {
      watcher_list->iterating = 1;
      QUEUE_MOVE(&watcher_list->watchers, &queue);
      while (!QUEUE_EMPTY(&queue)) {
        q = QUEUE_HEAD(&queue);
        handle = QUEUE_DATA(q, uv_fs_event_t, watchers);
        /* It's critical to keep a copy of path here, because it
         * will be set to NULL by stop() and then deallocated by
         * maybe_free_watcher_list
         */
        tmp_path = uv__strdup(handle->path);
        assert(tmp_path != NULL);
        QUEUE_REMOVE(q);
        QUEUE_INSERT_TAIL(&watcher_list->watchers, q);
        uv_fs_event_stop(handle);

        QUEUE_INSERT_TAIL(&tmp_watcher_list.watchers, &handle->watchers);
        handle->path = tmp_path;
      }
      watcher_list->iterating = 0;
      maybe_free_watcher_list(watcher_list, loop);
    }

    QUEUE_MOVE(&tmp_watcher_list.watchers, &queue);
    while (!QUEUE_EMPTY(&queue)) {
        q = QUEUE_HEAD(&queue);
        QUEUE_REMOVE(q);
        handle = QUEUE_DATA(q, uv_fs_event_t, watchers);
        tmp_path = handle->path;
        handle->path = NULL;
        err = uv_fs_event_start(handle, handle->cb, tmp_path, 0);
        uv__free(tmp_path);
        if (err)
          return err;
    }
  }

  return 0;
}


static struct watcher_list* find_watcher(uv_loop_t* loop, int wd) {
  struct watcher_list w;
  w.wd = wd;
  return RB_FIND(watcher_root, CAST(&loop->inotify_watchers), &w);
}


static void maybe_free_watcher_list(struct watcher_list* w, uv_loop_t* loop) {
  /* if the watcher_list->watchers is being iterated over, we can't free it. */
  if ((!w->iterating) && QUEUE_EMPTY(&w->watchers)) {
    /* No watchers left for this path. Clean up. */
    RB_REMOVE(watcher_root, CAST(&loop->inotify_watchers), w);
    inotify_rm_watch(loop->inotify_fd, w->wd);
    uv__free(w);
  }
}


static void uv__inotify_read(uv_loop_t* loop,
                             uv__io_t* dummy,
                             unsigned int events) {
  const struct inotify_event* e;
  struct watcher_list* w;
  uv_fs_event_t* h;
  QUEUE queue;
  QUEUE* q;
  const char* path;
  ssize_t size;
  const char *p;
  /* needs to be large enough for sizeof(inotify_event) + strlen(path) */
  char buf[4096];

  for (;;) {
    do
      size = read(loop->inotify_fd, buf, sizeof(buf));
    while (size == -1 && errno == EINTR);

    if (size == -1) {
      assert(errno == EAGAIN || errno == EWOULDBLOCK);
      break;
    }

    assert(size > 0); /* pre-2.6.21 thing, size=0 == read buffer too small */

    /* Now we have one or more inotify_event structs. */
    for (p = buf; p < buf + size; p += sizeof(*e) + e->len) {
      e = (const struct inotify_event*) p;

      events = 0;
      if (e->mask & (IN_ATTRIB|IN_MODIFY))
        events |= UV_CHANGE;
      if (e->mask & ~(IN_ATTRIB|IN_MODIFY))
        events |= UV_RENAME;

      w = find_watcher(loop, e->wd);
      if (w == NULL)
        continue; /* Stale event, no watchers left. */

      /* inotify does not return the filename when monitoring a single file
       * for modifications. Repurpose the filename for API compatibility.
       * I'm not convinced this is a good thing, maybe it should go.
       */
      path = e->len ? (const char*) (e + 1) : uv__basename_r(w->path);

      /* We're about to iterate over the queue and call user's callbacks.
       * What can go wrong?
       * A callback could call uv_fs_event_stop()
       * and the queue can change under our feet.
       * So, we use QUEUE_MOVE() trick to safely iterate over the queue.
       * And we don't free the watcher_list until we're done iterating.
       *
       * First,
       * tell uv_fs_event_stop() (that could be called from a user's callback)
       * not to free watcher_list.
       */
      w->iterating = 1;
      QUEUE_MOVE(&w->watchers, &queue);
      while (!QUEUE_EMPTY(&queue)) {
        q = QUEUE_HEAD(&queue);
        h = QUEUE_DATA(q, uv_fs_event_t, watchers);

        QUEUE_REMOVE(q);
        QUEUE_INSERT_TAIL(&w->watchers, q);

        h->cb(h, path, events, 0);
      }
      /* done iterating, time to (maybe) free empty watcher_list */
      w->iterating = 0;
      maybe_free_watcher_list(w, loop);
    }
  }
}


int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_FS_EVENT);
  return 0;
}


int uv_fs_event_start(uv_fs_event_t* handle,
                      uv_fs_event_cb cb,
                      const char* path,
                      unsigned int flags) {
  struct watcher_list* w;
  size_t len;
  int events;
  int err;
  int wd;

  if (uv__is_active(handle))
    return UV_EINVAL;

  err = init_inotify(handle->loop);
  if (err)
    return err;

  events = IN_ATTRIB
         | IN_CREATE
         | IN_MODIFY
         | IN_DELETE
         | IN_DELETE_SELF
         | IN_MOVE_SELF
         | IN_MOVED_FROM
         | IN_MOVED_TO;

  wd = inotify_add_watch(handle->loop->inotify_fd, path, events);
  if (wd == -1)
    return UV__ERR(errno);

  w = find_watcher(handle->loop, wd);
  if (w)
    goto no_insert;

  len = strlen(path) + 1;
  w = uv__malloc(sizeof(*w) + len);
  if (w == NULL)
    return UV_ENOMEM;

  w->wd = wd;
  w->path = memcpy(w + 1, path, len);
  QUEUE_INIT(&w->watchers);
  w->iterating = 0;
  RB_INSERT(watcher_root, CAST(&handle->loop->inotify_watchers), w);

no_insert:
  uv__handle_start(handle);
  QUEUE_INSERT_TAIL(&w->watchers, &handle->watchers);
  handle->path = w->path;
  handle->cb = cb;
  handle->wd = wd;

  return 0;
}


int uv_fs_event_stop(uv_fs_event_t* handle) {
  struct watcher_list* w;

  if (!uv__is_active(handle))
    return 0;

  w = find_watcher(handle->loop, handle->wd);
  assert(w != NULL);

  handle->wd = -1;
  handle->path = NULL;
  uv__handle_stop(handle);
  QUEUE_REMOVE(&handle->watchers);

  maybe_free_watcher_list(w, handle->loop);

  return 0;
}


void uv__fs_event_close(uv_fs_event_t* handle) {
  uv_fs_event_stop(handle);
}
