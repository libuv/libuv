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
#include "task.h"
#include <string.h>


TEST_IMPL(platform_output) {
  char buffer[512];
  size_t rss;
  size_t size;
  double uptime;
  uv_pid_t pid;
  uv_pid_t ppid;
  uv_rusage_t rusage;
  uv_cpu_info_t* cpus;
  uv_interface_address_t* interfaces;
  uv_network_interface_t* netifs;
  uv_passwd_t pwd;
  uv_utsname_t uname;
  int count;
  int i;
  int err;

  /* uv_get_process_title */
  err = uv_get_process_title(buffer, sizeof(buffer));
  ASSERT(err == 0);
  printf("uv_get_process_title: %s\n", buffer);

  /* uv_cwd */
  size = sizeof(buffer);
  err = uv_cwd(buffer, &size);
  ASSERT(err == 0);
  printf("uv_cwd: %s\n", buffer);

  /* uv_resident_set_memory */
  err = uv_resident_set_memory(&rss);
#if defined(__MSYS__)
  ASSERT(err == UV_ENOSYS);
#else
  ASSERT(err == 0);
  printf("uv_resident_set_memory: %llu\n", (unsigned long long) rss);
#endif

  /* uv_uptime */
  err = uv_uptime(&uptime);
  ASSERT(err == 0);
  ASSERT(uptime > 0);
  printf("uv_uptime: %f\n", uptime);

  /* uv_getrusage */
  err = uv_getrusage(&rusage);
  ASSERT(err == 0);
  ASSERT(rusage.ru_utime.tv_sec >= 0);
  ASSERT(rusage.ru_utime.tv_usec >= 0);
  ASSERT(rusage.ru_stime.tv_sec >= 0);
  ASSERT(rusage.ru_stime.tv_usec >= 0);
  printf("uv_getrusage:\n");
  printf("  user: %llu sec %llu microsec\n",
         (unsigned long long) rusage.ru_utime.tv_sec,
         (unsigned long long) rusage.ru_utime.tv_usec);
  printf("  system: %llu sec %llu microsec\n",
         (unsigned long long) rusage.ru_stime.tv_sec,
         (unsigned long long) rusage.ru_stime.tv_usec);
  printf("  page faults: %llu\n", (unsigned long long) rusage.ru_majflt);
  printf("  maximum resident set size: %llu\n",
         (unsigned long long) rusage.ru_maxrss);

  /* uv_cpu_info */
  err = uv_cpu_info(&cpus, &count);
#if defined(__CYGWIN__) || defined(__MSYS__)
  ASSERT(err == UV_ENOSYS);
#else
  ASSERT(err == 0);

  printf("uv_cpu_info:\n");
  for (i = 0; i < count; i++) {
    printf("  model: %s\n", cpus[i].model);
    printf("  speed: %d\n", cpus[i].speed);
    printf("  times.sys: %llu\n", (unsigned long long) cpus[i].cpu_times.sys);
    printf("  times.user: %llu\n",
           (unsigned long long) cpus[i].cpu_times.user);
    printf("  times.idle: %llu\n",
           (unsigned long long) cpus[i].cpu_times.idle);
    printf("  times.irq: %llu\n",  (unsigned long long) cpus[i].cpu_times.irq);
    printf("  times.nice: %llu\n",
           (unsigned long long) cpus[i].cpu_times.nice);
  }
#endif
  uv_free_cpu_info(cpus, count);

  /* uv_interface_addresses */
  err = uv_interface_addresses(&interfaces, &count);
  ASSERT(err == 0);

  printf("uv_interface_addresses:\n");
  for (i = 0; i < count; i++) {
    printf("  name: %s\n", interfaces[i].name);
    printf("  internal: %d\n", interfaces[i].is_internal);
    printf("  physical address: ");
    printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
           (unsigned char)interfaces[i].phys_addr[0],
           (unsigned char)interfaces[i].phys_addr[1],
           (unsigned char)interfaces[i].phys_addr[2],
           (unsigned char)interfaces[i].phys_addr[3],
           (unsigned char)interfaces[i].phys_addr[4],
           (unsigned char)interfaces[i].phys_addr[5]);

    if (interfaces[i].address.address4.sin_family == AF_INET) {
      uv_ip4_name(&interfaces[i].address.address4, buffer, sizeof(buffer));
    } else if (interfaces[i].address.address4.sin_family == AF_INET6) {
      uv_ip6_name(&interfaces[i].address.address6, buffer, sizeof(buffer));
    }

    printf("  address: %s\n", buffer);

    if (interfaces[i].netmask.netmask4.sin_family == AF_INET) {
      uv_ip4_name(&interfaces[i].netmask.netmask4, buffer, sizeof(buffer));
      printf("  netmask: %s\n", buffer);
    } else if (interfaces[i].netmask.netmask4.sin_family == AF_INET6) {
      uv_ip6_name(&interfaces[i].netmask.netmask6, buffer, sizeof(buffer));
      printf("  netmask: %s\n", buffer);
    } else {
      printf("  netmask: none\n");
    }
  }
  uv_free_interface_addresses(interfaces, count);

  /* uv_network_interfaces */
  err = uv_network_interfaces(&netifs, &count);
  ASSERT(err == 0 || err == UV_ENOTSUP);

  /* TODO: Remove this outer `if` after all platforms have implemented the new
   * interface and the `ASSERT` above drops the `|| err == UV_ENOTSUP`
   * condition.
   */
  if (err == 0) {
    printf("uv_network_interfaces:\n");
    for (i = 0; i < count; i++) {

      /* Meta & flags */
      printf("  name: %s\n", netifs[i].name);
      printf("    is_up: %d\n", UV_NETIF_IS_UP(netifs[i]));
      printf("    is_loopback: %d\n", UV_NETIF_IS_LOOPBACK(netifs[i]));
      printf("    is_point_to_point: %d\n", UV_NETIF_IS_PTP(netifs[i]));
      printf("    is_promiscuous: %d\n", UV_NETIF_IS_PROMISCUOUS(netifs[i]));
      printf("    has_broadcast: %d\n", UV_NETIF_HAS_BROADCAST(netifs[i]));
      printf("    has_multicast: %d\n", UV_NETIF_HAS_MULTICAST(netifs[i]));

      /* Address */
      if (netifs[i].address.address4.sin_family == AF_INET6) {
        /* IPv6 addresses will display the prefix directly after the address,
         * while IPv4 addresses will later print a netmask address computed
         * from the prefix.
         */
        uv_ip6_name(&netifs[i].address.address6, buffer, sizeof(buffer));
        printf("    address: inet6 %s/%u\n", buffer, netifs[i].prefix);
      } else if (netifs[i].address.address4.sin_family == AF_INET) {
        uv_ip4_name(&netifs[i].address.address4, buffer, sizeof(buffer));
        printf("    address: inet4 %s\n", buffer);
      } else {
        printf("    address: none\n");
      }

      /* Broadcast */
      if (UV_NETIF_HAS_BROADCAST(netifs[i]) &&
          netifs[i].broadcast4.sin_family == AF_INET) {
        uv_ip4_name(&netifs[i].broadcast4, buffer, sizeof(buffer));
        printf("    broadcast: inet4 %s\n", buffer);
      }

      /* Netmask */
      if (netifs[i].address.address4.sin_family == AF_INET) {
        /* Only compute and display an IPv4 style netmask from the CIDR prefix
         * if the interface address is `AF_INET`
         */
        if (netifs[i].prefix) {
          uint32_t mask = htonl(~((1 << (32 - netifs[i].prefix)) - 1));
          struct sockaddr_in netmask4;
          netmask4.sin_addr.s_addr = mask;
          uv_ip4_name(&netmask4, buffer, sizeof(buffer));
          printf("    netmask: inet4 %s\n", buffer);
        } else {
          printf("    netmask: none\n");
        }
      }

      /* Physical layer */
      if (netifs[i].phys_addr_len) {
        uint32_t b;
        printf("    physical address: ");
        for (b = 0; b < netifs[i].phys_addr_len; ++b) {
          printf("%s%02x", b == 0 ? "" : ":",
                 (unsigned char)netifs[i].phys_addr[b]);
        }
        printf("\n");
      }
    }

    uv_free_network_interfaces(netifs, count);
  }

  /* uv_os_get_passwd */
  err = uv_os_get_passwd(&pwd);
  ASSERT(err == 0);

  printf("uv_os_get_passwd:\n");
  printf("  euid: %ld\n", pwd.uid);
  printf("  gid: %ld\n", pwd.gid);
  printf("  username: %s\n", pwd.username);
  printf("  shell: %s\n", pwd.shell);
  printf("  home directory: %s\n", pwd.homedir);

  pid = uv_os_getpid();
  ASSERT(pid > 0);
  printf("uv_os_getpid: %d\n", (int) pid);
  ppid = uv_os_getppid();
  ASSERT(ppid > 0);
  printf("uv_os_getppid: %d\n", (int) ppid);

  err = uv_os_uname(&uname);
  ASSERT(err == 0);
  printf("uv_os_uname:\n");
  printf("  sysname: %s\n", uname.sysname);
  printf("  release: %s\n", uname.release);
  printf("  version: %s\n", uname.version);
  printf("  machine: %s\n", uname.machine);

  return 0;
}
