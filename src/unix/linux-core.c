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

#include <net/if.h>
#include <sys/epoll.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

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

/* Constant values for cgroups. */
#define PROC_SELF_MOUNTINFO "/proc/self/mountinfo"
#define PROC_SELF_CGROUP "/proc/self/cgroup"
#define CGROUPS_VERSION_UNKNOWN 0x0
#define CGROUPS_VERSION_1 0x1
#define CGROUPS_VERSION_2 0x2

static int read_models(unsigned int numcpus, uv_cpu_info_t* ci);
static int read_times(FILE* statfile_fp,
                      unsigned int numcpus,
                      uv_cpu_info_t* ci);
static void read_speeds(unsigned int numcpus, uv_cpu_info_t* ci);
static uint64_t read_cpufreq(unsigned int cpunum);

int uv__platform_loop_init(uv_loop_t* loop) {
  
  loop->inotify_fd = -1;
  loop->inotify_watchers = NULL;

  return uv__epoll_init(loop);
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


/*
 * Holds information about a given cgroups subsystem.
 */
typedef struct {
  /* cgroups version, one of CGROUPS_VERSION_*. */
  uint8_t cgroups_version;
  /* Path in which param files are found for a subsystem. */
  char* path;
} uv__cgroups_subsystem_info_t;


/*
 * Given a `separator`-delimited string `haystack`, return 1 if `needle` exactly
 * matches at least one of the elements in that sequence, 0 if this is not the
 * case or an error occurred (due to invalid inputs or OOM).
 */
static int uv__find_in_delimited_string(const char* haystack,
                                        const char* needle,
                                        const char* separator) {
  char* haystack_mutable;
  size_t haystack_len;
  char* candidate;
  char* haystack_ptr;
  haystack_len = strlen(haystack);
  if (needle == NULL || strlen(needle) > haystack_len)
    return 0;
  haystack_mutable = uv__strndup(haystack, haystack_len);
  if (haystack_mutable == NULL)
    return 0;
  haystack_ptr = haystack_mutable;
  do {
    candidate = strsep(&haystack_ptr, separator);
    if (strcmp(candidate, needle) == 0) {
      uv__free(haystack_mutable);
      return 1;
    }
  } while (haystack_ptr != NULL);
  uv__free(haystack_mutable);
  return 0;
}


/*
 * Read /proc/self/mountinfo and /proc/self/cgroup to get info about how the
 * given subsystem is controlled for this process, and the path to the
 * subsystem's parameter files, if possible.
 * 
 * If info->cgroups_version == CGROUPS_VERSION_UNKNOWN, info->path will be NULL.
 * Otherwise, info->path will be a heap pointer and the caller is responsible
 * for freeing it.
 * 
 * == cgroups v1 example ==
 * 
 * /proc/self/mountinfo:
 * ...
 * 490 486 0:31 /docker/8b1b53f /sys/fs/cgroup/blkio ro master:21 - cgroup blkio rw,blkio
 * 491 486 0:32 /docker/8b1b53f /sys/fs/cgroup/memory ro master:22 - cgroup memory rw,memory
 * 492 486 0:33 /docker/8b1b53f /sys/fs/cgroup/devices ro master:23 - cgroup devices rw,devices
 * ...
 * 
 * /proc/self/cgroup
 * ...
 * 6:devices:/docker/8b1b53f/foo-slice
 * 5:memory:/docker/8b1b53f/foo-slice
 * 4:blkio:/docker/8b1b53f/foo-slice
 * ...
 * 
 * If we are looking for the path to the memory parameter files, we substitute
 * the portion of the path corresponding to "memory" in /proc/self/cgroup that
 * matches the root in /proc/self/mountinfo (/docker/8b1b53f) with the mount
 * point (/sys/fs/cgroup/memory), resulting in the path
 * /sys/fs/cgroup/memory/foo-slice.
 * 
 * == cgroups v2 example ==
 * 
 * /proc/self/mountinfo:
 * ...
 * 26 17 0:22 / /sys/fs/cgroup rw shared:9 - cgroup2 cgroup rw
 * ...
 * 
 * /proc/self/cgroup (in entirety):
 * 0::/foo-slice
 * 
 * If we are looking for the path to the memory parameter files, we substitute
 * the portion of the path in the only entry in /proc/self/cgroup that matches
 * the root (/) with the mount point (/sys/fs/cgroup), resulting in the path
 * /sys/fs/cgroup/foo-slice.
 */
static int uv__read_cgroups_proc_files(uv__cgroups_subsystem_info_t* info,
                                       const char* subsystem) {
  int rc;
  FILE* fp;
  /* Buffer to be dynamically (re-)sized by getline(). */
  char* buf;
  /*
   * Length of the string contained in `buf` immediately after it's written by
   * getline().
   */
  size_t buf_strlen;
  /* Current allocated size of `buf`. */
  size_t buf_size;

  /* From /proc/self/mountinfo */
  char* root;
  char* mount_point;
  /* From /proc/self/cgroup */
  char* hierarchy_path;

  /* Values used when reading /proc/self/mountinfo */
  char* field_ptr;
  char* curr_root;
  char* curr_mount_point;
  char* curr_fs_type;
  char* curr_super_options;
  /* Values used when reading /proc/self/cgroup */
  const char* hierarchy_path_search_ptr;
  const char* hierarchy_path_inner;
  char* subsystem_search_string;

  rc = 0;
  fp = NULL;
  buf = NULL;
  buf_strlen = 0;
  buf_size = 0;
  root = NULL;
  mount_point = NULL;
  hierarchy_path = NULL;
  subsystem_search_string = NULL;

  info->cgroups_version = CGROUPS_VERSION_UNKNOWN;
  info->path = NULL;

  /* Read /proc/self/mountinfo to get controller path. */

  fp = uv__open_file(PROC_SELF_MOUNTINFO);
  if (fp == NULL) {
    rc = UV__ERR(errno);
    goto cleanup;
  }

  root = uv__malloc(UV__PATH_MAX);
  mount_point = uv__malloc(UV__PATH_MAX);
  if (root == NULL || mount_point == NULL) {
    rc = UV_ENOMEM;
    goto cleanup;
  }

  /*
   * Loop once per line; try to find the mount location for the given subsystem.
   */
  while ((buf_strlen = getline(&buf, &buf_size, fp)) != -1) {
    if ('\n' == buf[buf_strlen - 1])
      buf[buf_strlen - 1] = '\0';

    field_ptr = buf;
    strsep(&field_ptr, " "); /* mount ID */
    strsep(&field_ptr, " "); /* parent ID */
    strsep(&field_ptr, " "); /* st_dev major:minor */
    curr_root = strsep(&field_ptr, " ");
    if (curr_root == NULL)
      continue;
    curr_mount_point = strsep(&field_ptr, " ");
    if (curr_mount_point == NULL)
      continue;
    strsep(&field_ptr, " "); /* mount options */
    /* A hyphen marks the end of variable-length optional fields. */
    while (field_ptr != NULL && '-' != field_ptr[0])
      strsep(&field_ptr, " ");
    strsep(&field_ptr, " "); /* separator (hyphen) */
    curr_fs_type = strsep(&field_ptr, " ");
    if (curr_fs_type == NULL)
      continue;
    strsep(&field_ptr, " "); /* mount source */
    curr_super_options = strsep(&field_ptr, " ");
    if (curr_super_options == NULL)
      continue;

    /*
     * If the fs type (9) is "cgroup" and super options (11) contains the name
     * of the subsystem, then we've found the correct mount location, so save
     * the values of root (4) and mount point (5) and break.
     * Otherwise, if the fs type is "cgroup2", we've potentially found the
     * correct mount location, so save the above values, but don't break,
     * because we don't know yet whether the input subsystem is controlled by
     * cgroups v1 or v2.
     */
    if (strcmp(curr_fs_type, "cgroup") == 0) {
      /* cgroups v1 */
      if (0 !=
          uv__find_in_delimited_string(curr_super_options, subsystem, ",")) {
        if (uv__strscpy(root, curr_root, UV__PATH_MAX - 1) < 0 ||
            uv__strscpy(mount_point, curr_mount_point, UV__PATH_MAX - 1) < 0) {
          rc = UV_E2BIG;
          goto cleanup;
        }
        info->cgroups_version = CGROUPS_VERSION_1;
        break;
      }
    } else if (strcmp(curr_fs_type, "cgroup2") == 0) {
      /* cgroups v2 */
      if (uv__strscpy(root, curr_root, UV__PATH_MAX - 1) < 0 ||
          uv__strscpy(mount_point, curr_mount_point, UV__PATH_MAX - 1) < 0) {
        rc = UV_E2BIG;
        goto cleanup;
      }
      info->cgroups_version = CGROUPS_VERSION_2;
      /*
       * Don't break, as we're not certain that this subsystem is controlled
       * by cgroups v2.
       */
    }
  }

  if (ferror(fp)) {
    rc = UV__ERR(errno);
    goto cleanup;
  }

  fclose(fp);
  fp = NULL;
  free(buf);
  buf = NULL;
  buf_strlen = 0;
  buf_size = 0;

  /*
   * If cgroups version wasn't determined, assume this subsystem isn't enabled
   * in cgroups, so don't bother reading /proc/self/cgroup.
   */
  if (CGROUPS_VERSION_UNKNOWN == info->cgroups_version) {
    goto cleanup;
  }
  
  /* Read /proc/self/cgroup to get hierarchy path. */

  fp = uv__open_file(PROC_SELF_CGROUP);
  if (fp == NULL) {
    rc = UV__ERR(errno);
    goto cleanup;
  }
  
  hierarchy_path = uv__malloc(UV__PATH_MAX);
  /* + 3 for two colons, and terminal character. */
  subsystem_search_string = uv__malloc(strlen(subsystem) + 3);
  if (hierarchy_path == NULL || subsystem_search_string == NULL) {
    rc = UV_ENOMEM;
    goto cleanup;
  }

  hierarchy_path[0] = '\0';
  snprintf(subsystem_search_string, strlen(subsystem) + 3, ":%s:", subsystem);

  while (feof(fp) == 0) {
    if (getline(&buf, &buf_size, fp) < 0) {
      if (feof(fp) != 0)
        break;
      rc = UV_EIO;
      goto cleanup;
    }
    buf_strlen = strlen(buf);
    if ('\n' == buf[buf_strlen - 1])
      buf[buf_strlen - 1] = '\0';

    if (CGROUPS_VERSION_1 == info->cgroups_version) {
      hierarchy_path_search_ptr = strstr(buf, subsystem_search_string);
      if (hierarchy_path_search_ptr != NULL)
        hierarchy_path_search_ptr += strlen(subsystem_search_string);
    } else { /* if (CGROUPS_VERSION_2 == info->cgroups_version) */
      /* 3 is the string length of "0::". */
      if (strncmp(buf, "0::", 3) == 0)
        hierarchy_path_search_ptr = buf + 3;
    }
    if (hierarchy_path_search_ptr != NULL) {
      if (uv__strscpy(hierarchy_path,
                      hierarchy_path_search_ptr,
                      UV__PATH_MAX - 1) < 0) {
        rc = UV_E2BIG;
        goto cleanup;
      }
      break;
    }
  }

  fclose(fp);
  fp = NULL;

  /*
   * The hierarchy path should be prefixed with the root path from mountinfo,
   * and should be replaced with the mount point.
   */
  if (strncmp(hierarchy_path, root, strlen(root)) == 0) {
    size_t path_size;
    hierarchy_path_inner = hierarchy_path + strlen(root);
    /* +2 for "/" and null terminator. */
    path_size = strlen(mount_point) + strlen(hierarchy_path_inner) + 2;
    info->path = uv__malloc(path_size);
    if (info->path == NULL)
      rc = UV_ENOMEM;
    else
      snprintf(info->path,
               path_size,
               "%s/%s",
               mount_point,
               hierarchy_path_inner);
  } else {
    info->cgroups_version = CGROUPS_VERSION_UNKNOWN;
  }


cleanup:
  if (fp != NULL)
    fclose(fp);
  /* buf is (re-)allocated via getline, so use standard free. */
  free(buf);
  uv__free(root);
  uv__free(mount_point);
  uv__free(hierarchy_path);
  uv__free(subsystem_search_string);
  return rc;
}


static uint64_t uv__read_cgroups_uint64(const char* path, const char* param) {

  char filename[UV__PATH_MAX];
  char buf[32];  /* Large enough to hold an encoded uint64_t. */
  uint64_t rc = 0;

  snprintf(filename, sizeof(filename), "%s/%s", path, param);
  if (0 == uv__slurp(filename, buf, sizeof(buf)))
    if (0 != strcmp(buf, "max"))
      sscanf(buf, "%" PRIu64, &rc);

  return rc;
}


uint64_t uv_get_constrained_memory(void) {
  uv__cgroups_subsystem_info_t info;
  uint64_t rc;
  /* For v2 only. */
  uint64_t max;
  uint64_t high;

  rc = 0;

  if (uv__read_cgroups_proc_files(&info, "memory") == 0) {
    /*
     * uv__read_cgroups_uint64 might return 0 if there was a problem getting the
     * memory limit from cgroups. This is OK because a return value of 0
     * signifies that the memory limit is unknown.
     */

    if (CGROUPS_VERSION_1 == info.cgroups_version)
      rc = uv__read_cgroups_uint64(info.path, "memory.limit_in_bytes");
    else if (CGROUPS_VERSION_2 == info.cgroups_version) {
      max = uv__read_cgroups_uint64(info.path, "memory.max");
      high = uv__read_cgroups_uint64(info.path, "memory.high");
      if (max == 0)
        rc = high;
      else if (high == 0)
        rc = max;
      else
        rc = max < high ? max : high;
    }

    uv__free(info.path);
  }

  return rc;
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
