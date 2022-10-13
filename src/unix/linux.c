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

#ifndef __NR_recvmmsg
# if defined(__x86_64__)
#  define __NR_recvmmsg 299
# elif defined(__arm__)
#  define __NR_recvmmsg (UV_SYSCALL_BASE + 365)
# endif
#endif /* __NR_recvmsg */

#ifndef __NR_sendmmsg
# if defined(__x86_64__)
#  define __NR_sendmmsg 307
# elif defined(__arm__)
#  define __NR_sendmmsg (UV_SYSCALL_BASE + 374)
# endif
#endif /* __NR_sendmmsg */

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

/* Available from 2.6.32 onwards. */
#ifndef CLOCK_MONOTONIC_COARSE
# define CLOCK_MONOTONIC_COARSE 6
#endif

/* This is rather annoying: CLOCK_BOOTTIME lives in <linux/time.h> but we can't
 * include that file because it conflicts with <time.h>. We'll just have to
 * define it ourselves.
 */
#ifndef CLOCK_BOOTTIME
# define CLOCK_BOOTTIME 7
#endif

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
static int read_models(unsigned int numcpus, uv_cpu_info_t* ci);
static int read_times(FILE* statfile_fp,
                      unsigned int numcpus,
                      uv_cpu_info_t* ci);
static void read_speeds(unsigned int numcpus, uv_cpu_info_t* ci);
static uint64_t read_cpufreq(unsigned int cpunum);

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

    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
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
  static volatile int no_clock_boottime;
  char buf[128];
  struct timespec now;
  int r;

  /* Try /proc/uptime first, then fallback to clock_gettime(). */

  if (0 == uv__slurp("/proc/uptime", buf, sizeof(buf)))
    if (1 == sscanf(buf, "%lf", uptime))
      return 0;

  /* Try CLOCK_BOOTTIME first, fall back to CLOCK_MONOTONIC if not available
   * (pre-2.6.39 kernels). CLOCK_MONOTONIC doesn't increase when the system
   * is suspended.
   */
  if (no_clock_boottime) {
    retry_clock_gettime: r = clock_gettime(CLOCK_MONOTONIC, &now);
  }
  else if ((r = clock_gettime(CLOCK_BOOTTIME, &now)) && errno == EINVAL) {
    no_clock_boottime = 1;
    goto retry_clock_gettime;
  }

  if (r)
    return UV__ERR(errno);

  *uptime = now.tv_sec;
  return 0;
}


static int uv__cpu_num(FILE* statfile_fp, unsigned int* numcpus) {
  unsigned int num;
  char buf[1024];

  if (!fgets(buf, sizeof(buf), statfile_fp))
    return UV_EIO;

  num = 0;
  while (fgets(buf, sizeof(buf), statfile_fp)) {
    if (strncmp(buf, "cpu", 3))
      break;
    num++;
  }

  if (num == 0)
    return UV_EIO;

  *numcpus = num;
  return 0;
}


int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  unsigned int numcpus;
  uv_cpu_info_t* ci;
  int err;
  FILE* statfile_fp;

  *cpu_infos = NULL;
  *count = 0;

  statfile_fp = uv__open_file("/proc/stat");
  if (statfile_fp == NULL)
    return UV__ERR(errno);

  err = uv__cpu_num(statfile_fp, &numcpus);
  if (err < 0)
    goto out;

  err = UV_ENOMEM;
  ci = uv__calloc(numcpus, sizeof(*ci));
  if (ci == NULL)
    goto out;

  err = read_models(numcpus, ci);
  if (err == 0)
    err = read_times(statfile_fp, numcpus, ci);

  if (err) {
    uv_free_cpu_info(ci, numcpus);
    goto out;
  }

  /* read_models() on x86 also reads the CPU speed from /proc/cpuinfo.
   * We don't check for errors here. Worst case, the field is left zero.
   */
  if (ci[0].speed == 0)
    read_speeds(numcpus, ci);

  *cpu_infos = ci;
  *count = numcpus;
  err = 0;

out:

  if (fclose(statfile_fp))
    if (errno != EINTR && errno != EINPROGRESS)
      abort();

  return err;
}


static void read_speeds(unsigned int numcpus, uv_cpu_info_t* ci) {
  unsigned int num;

  for (num = 0; num < numcpus; num++)
    ci[num].speed = read_cpufreq(num) / 1000;
}


/* Also reads the CPU frequency on ppc and x86. The other architectures only
 * have a BogoMIPS field, which may not be very accurate.
 *
 * Note: Simply returns on error, uv_cpu_info() takes care of the cleanup.
 */
static int read_models(unsigned int numcpus, uv_cpu_info_t* ci) {
#if defined(__PPC__)
  static const char model_marker[] = "cpu\t\t: ";
  static const char speed_marker[] = "clock\t\t: ";
#else
  static const char model_marker[] = "model name\t: ";
  static const char speed_marker[] = "cpu MHz\t\t: ";
#endif
  const char* inferred_model;
  unsigned int model_idx;
  unsigned int speed_idx;
  unsigned int part_idx;
  char buf[1024];
  char* model;
  FILE* fp;
  int model_id;

  /* Most are unused on non-ARM, non-MIPS and non-x86 architectures. */
  (void) &model_marker;
  (void) &speed_marker;
  (void) &speed_idx;
  (void) &part_idx;
  (void) &model;
  (void) &buf;
  (void) &fp;
  (void) &model_id;

  model_idx = 0;
  speed_idx = 0;
  part_idx = 0;

#if defined(__arm__) || \
    defined(__i386__) || \
    defined(__mips__) || \
    defined(__aarch64__) || \
    defined(__PPC__) || \
    defined(__x86_64__)
  fp = uv__open_file("/proc/cpuinfo");
  if (fp == NULL)
    return UV__ERR(errno);

  while (fgets(buf, sizeof(buf), fp)) {
    if (model_idx < numcpus) {
      if (strncmp(buf, model_marker, sizeof(model_marker) - 1) == 0) {
        model = buf + sizeof(model_marker) - 1;
        model = uv__strndup(model, strlen(model) - 1);  /* Strip newline. */
        if (model == NULL) {
          fclose(fp);
          return UV_ENOMEM;
        }
        ci[model_idx++].model = model;
        continue;
      }
    }
#if defined(__arm__) || defined(__mips__) || defined(__aarch64__)
    if (model_idx < numcpus) {
#if defined(__arm__)
      /* Fallback for pre-3.8 kernels. */
      static const char model_marker[] = "Processor\t: ";
#elif defined(__aarch64__)
      static const char part_marker[] = "CPU part\t: ";

      /* Adapted from: https://github.com/karelzak/util-linux */
      struct vendor_part {
        const int id;
        const char* name;
      };

      static const struct vendor_part arm_chips[] = {
        { 0x811, "ARM810" },
        { 0x920, "ARM920" },
        { 0x922, "ARM922" },
        { 0x926, "ARM926" },
        { 0x940, "ARM940" },
        { 0x946, "ARM946" },
        { 0x966, "ARM966" },
        { 0xa20, "ARM1020" },
        { 0xa22, "ARM1022" },
        { 0xa26, "ARM1026" },
        { 0xb02, "ARM11 MPCore" },
        { 0xb36, "ARM1136" },
        { 0xb56, "ARM1156" },
        { 0xb76, "ARM1176" },
        { 0xc05, "Cortex-A5" },
        { 0xc07, "Cortex-A7" },
        { 0xc08, "Cortex-A8" },
        { 0xc09, "Cortex-A9" },
        { 0xc0d, "Cortex-A17" },  /* Originally A12 */
        { 0xc0f, "Cortex-A15" },
        { 0xc0e, "Cortex-A17" },
        { 0xc14, "Cortex-R4" },
        { 0xc15, "Cortex-R5" },
        { 0xc17, "Cortex-R7" },
        { 0xc18, "Cortex-R8" },
        { 0xc20, "Cortex-M0" },
        { 0xc21, "Cortex-M1" },
        { 0xc23, "Cortex-M3" },
        { 0xc24, "Cortex-M4" },
        { 0xc27, "Cortex-M7" },
        { 0xc60, "Cortex-M0+" },
        { 0xd01, "Cortex-A32" },
        { 0xd03, "Cortex-A53" },
        { 0xd04, "Cortex-A35" },
        { 0xd05, "Cortex-A55" },
        { 0xd06, "Cortex-A65" },
        { 0xd07, "Cortex-A57" },
        { 0xd08, "Cortex-A72" },
        { 0xd09, "Cortex-A73" },
        { 0xd0a, "Cortex-A75" },
        { 0xd0b, "Cortex-A76" },
        { 0xd0c, "Neoverse-N1" },
        { 0xd0d, "Cortex-A77" },
        { 0xd0e, "Cortex-A76AE" },
        { 0xd13, "Cortex-R52" },
        { 0xd20, "Cortex-M23" },
        { 0xd21, "Cortex-M33" },
        { 0xd41, "Cortex-A78" },
        { 0xd42, "Cortex-A78AE" },
        { 0xd4a, "Neoverse-E1" },
        { 0xd4b, "Cortex-A78C" },
      };

      if (strncmp(buf, part_marker, sizeof(part_marker) - 1) == 0) {
        model = buf + sizeof(part_marker) - 1;

        errno = 0;
        model_id = strtol(model, NULL, 16);
        if ((errno != 0) || model_id < 0) {
          fclose(fp);
          return UV_EINVAL;
        }

        for (part_idx = 0; part_idx < ARRAY_SIZE(arm_chips); part_idx++) {
          if (model_id == arm_chips[part_idx].id) {
            model = uv__strdup(arm_chips[part_idx].name);
            if (model == NULL) {
              fclose(fp);
              return UV_ENOMEM;
            }
            ci[model_idx++].model = model;
            break;
          }
        }
      }
#else	/* defined(__mips__) */
      static const char model_marker[] = "cpu model\t\t: ";
#endif
      if (strncmp(buf, model_marker, sizeof(model_marker) - 1) == 0) {
        model = buf + sizeof(model_marker) - 1;
        model = uv__strndup(model, strlen(model) - 1);  /* Strip newline. */
        if (model == NULL) {
          fclose(fp);
          return UV_ENOMEM;
        }
        ci[model_idx++].model = model;
        continue;
      }
    }
#else  /* !__arm__ && !__mips__ && !__aarch64__ */
    if (speed_idx < numcpus) {
      if (strncmp(buf, speed_marker, sizeof(speed_marker) - 1) == 0) {
        ci[speed_idx++].speed = atoi(buf + sizeof(speed_marker) - 1);
        continue;
      }
    }
#endif  /* __arm__ || __mips__ || __aarch64__ */
  }

  fclose(fp);
#endif  /* __arm__ || __i386__ || __mips__ || __PPC__ || __x86_64__ || __aarch__ */

  /* Now we want to make sure that all the models contain *something* because
   * it's not safe to leave them as null. Copy the last entry unless there
   * isn't one, in that case we simply put "unknown" into everything.
   */
  inferred_model = "unknown";
  if (model_idx > 0)
    inferred_model = ci[model_idx - 1].model;

  while (model_idx < numcpus) {
    model = uv__strndup(inferred_model, strlen(inferred_model));
    if (model == NULL)
      return UV_ENOMEM;
    ci[model_idx++].model = model;
  }

  return 0;
}


static int read_times(FILE* statfile_fp,
                      unsigned int numcpus,
                      uv_cpu_info_t* ci) {
  struct uv_cpu_times_s ts;
  unsigned int ticks;
  unsigned int multiplier;
  uint64_t user;
  uint64_t nice;
  uint64_t sys;
  uint64_t idle;
  uint64_t dummy;
  uint64_t irq;
  uint64_t num;
  uint64_t len;
  char buf[1024];

  ticks = (unsigned int)sysconf(_SC_CLK_TCK);
  assert(ticks != (unsigned int) -1);
  assert(ticks != 0);
  multiplier = ((uint64_t)1000L / ticks);

  rewind(statfile_fp);

  if (!fgets(buf, sizeof(buf), statfile_fp))
    abort();

  num = 0;

  while (fgets(buf, sizeof(buf), statfile_fp)) {
    if (num >= numcpus)
      break;

    if (strncmp(buf, "cpu", 3))
      break;

    /* skip "cpu<num> " marker */
    {
      unsigned int n;
      int r = sscanf(buf, "cpu%u ", &n);
      assert(r == 1);
      (void) r;  /* silence build warning */
      for (len = sizeof("cpu0"); n /= 10; len++);
    }

    /* Line contains user, nice, system, idle, iowait, irq, softirq, steal,
     * guest, guest_nice but we're only interested in the first four + irq.
     *
     * Don't use %*s to skip fields or %ll to read straight into the uint64_t
     * fields, they're not allowed in C89 mode.
     */
    if (6 != sscanf(buf + len,
                    "%" PRIu64 " %" PRIu64 " %" PRIu64
                    "%" PRIu64 " %" PRIu64 " %" PRIu64,
                    &user,
                    &nice,
                    &sys,
                    &idle,
                    &dummy,
                    &irq))
      abort();

    ts.user = user * multiplier;
    ts.nice = nice * multiplier;
    ts.sys  = sys * multiplier;
    ts.idle = idle * multiplier;
    ts.irq  = irq * multiplier;
    ci[num++].cpu_times = ts;
  }
  assert(num == numcpus);

  return 0;
}


static uint64_t read_cpufreq(unsigned int cpunum) {
  uint64_t val;
  char buf[1024];
  FILE* fp;

  snprintf(buf,
           sizeof(buf),
           "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq",
           cpunum);

  fp = uv__open_file(buf);
  if (fp == NULL)
    return 0;

  if (fscanf(fp, "%" PRIu64, &val) != 1)
    val = 0;

  fclose(fp);

  return val;
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


/* This might return 0 if there was a problem getting the memory limit from
 * cgroups. This is OK because a return value of 0 signifies that the memory
 * limit is unknown.
 */
static uint64_t uv__get_constrained_memory_fallback(void) {
  return uv__read_uint64("/sys/fs/cgroup/memory/memory.limit_in_bytes");
}


uint64_t uv_get_constrained_memory(void) {
  char filename[4097];
  char buf[1024];
  uint64_t high;
  uint64_t max;
  char* p;

  if (uv__slurp("/proc/self/cgroup", buf, sizeof(buf)))
    return uv__get_constrained_memory_fallback();

  if (memcmp(buf, "0::/", 4))
    return uv__get_constrained_memory_fallback();

  p = strchr(buf, '\n');
  if (p != NULL)
    *p = '\0';

  p = buf + 4;

  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%s/memory.max", p);
  max = uv__read_uint64(filename);

  if (max == 0)
    return uv__get_constrained_memory_fallback();

  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%s/memory.high", p);
  high = uv__read_uint64(filename);

  if (high == 0)
    return uv__get_constrained_memory_fallback();

  return high < max ? high : max;
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


int uv__sendmmsg(int fd, struct uv__mmsghdr* mmsg, unsigned int vlen) {
#if defined(__i386__)
  unsigned long args[4];
  int rc;

  args[0] = (unsigned long) fd;
  args[1] = (unsigned long) mmsg;
  args[2] = (unsigned long) vlen;
  args[3] = /* flags */ 0;

  /* socketcall() raises EINVAL when SYS_SENDMMSG is not supported. */
  rc = syscall(/* __NR_socketcall */ 102, 20 /* SYS_SENDMMSG */, args);
  if (rc == -1)
    if (errno == EINVAL)
      errno = ENOSYS;

  return rc;
#elif defined(__NR_sendmmsg)
  return syscall(__NR_sendmmsg, fd, mmsg, vlen, /* flags */ 0);
#else
  return errno = ENOSYS, -1;
#endif
}


static void uv__recvmmsg_unpoison(struct uv__mmsghdr* mmsg, int rc) {
  struct uv__mmsghdr* m;
  struct msghdr* h;
  struct iovec* v;
  size_t j;
  int i;

  for (i = 0; i < rc; i++) {
    m = mmsg + i;
    uv__msan_unpoison(m, sizeof(*m));

    h = &m->msg_hdr;
    if (h->msg_name != NULL)
      uv__msan_unpoison(h->msg_name, h->msg_namelen);

    if (h->msg_iov != NULL)
      uv__msan_unpoison(h->msg_iov, h->msg_iovlen * sizeof(*h->msg_iov));

    for (j = 0; j < h->msg_iovlen; j++) {
      v = h->msg_iov + j;
      uv__msan_unpoison(v->iov_base, v->iov_len);
    }

    if (h->msg_control != NULL)
      uv__msan_unpoison(h->msg_control, h->msg_controllen);
  }
}


int uv__recvmmsg(int fd, struct uv__mmsghdr* mmsg, unsigned int vlen) {
#if defined(__i386__)
  unsigned long args[5];
  int rc;

  args[0] = (unsigned long) fd;
  args[1] = (unsigned long) mmsg;
  args[2] = (unsigned long) vlen;
  args[3] = /* flags */ 0;
  args[4] = /* timeout */ 0;

  /* socketcall() raises EINVAL when SYS_RECVMMSG is not supported. */
  rc = syscall(/* __NR_socketcall */ 102, 19 /* SYS_RECVMMSG */, args);
  uv__recvmmsg_unpoison(mmsg, rc);
  if (rc == -1)
    if (errno == EINVAL)
      errno = ENOSYS;

  return rc;
#elif defined(__NR_recvmmsg)
  int rc;

  rc = syscall(__NR_recvmmsg, fd, mmsg, vlen, /* flags */ 0, /* timeout */ 0);
  uv__recvmmsg_unpoison(mmsg, rc);

  return rc;
#else
  return errno = ENOSYS, -1;
#endif
}
