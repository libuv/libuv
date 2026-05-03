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
#include "internal.h"

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__MVS__)
#include <xti.h>
#endif
#include <sys/un.h>

#if defined(__linux__)
#include <linux/errqueue.h>
#endif

/* BSD/macOS need net/if_dl.h for struct sockaddr_dl (IP_RECVIF path). */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
#include <net/if_dl.h>
#endif

#ifndef SOL_UDP
# define SOL_UDP 17
#endif
#ifndef UDP_SEGMENT
# define UDP_SEGMENT 103
#endif
#ifndef UDP_GRO
# define UDP_GRO 104
#endif
#ifndef SO_TXTIME
# define SO_TXTIME 61
#endif
#ifndef SCM_TXTIME
# define SCM_TXTIME SO_TXTIME
#endif
#ifndef SO_INCOMING_CPU
# define SO_INCOMING_CPU 49
#endif

#if defined(__linux__)
struct uv__sock_txtime {
  uint32_t clockid;    /* CLOCK_TAI = 11 */
  uint32_t flags;      /* 0 or SOF_TXTIME_DEADLINE_MODE etc */
};
# ifndef CLOCK_TAI
#  define CLOCK_TAI 11
# endif
#endif

#if defined(IPV6_JOIN_GROUP) && !defined(IPV6_ADD_MEMBERSHIP)
# define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif

#if defined(IPV6_LEAVE_GROUP) && !defined(IPV6_DROP_MEMBERSHIP)
# define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

static void uv__udp_run_completed(uv_udp_t* handle);
static void uv__udp_recvmsg(uv_udp_t* handle, int flag);
static void uv__udp_sendmsg(uv_udp_t* handle);
static int uv__udp_maybe_deferred_bind(uv_udp_t* handle,
                                       int domain,
                                       unsigned int flags);
static int uv__udp_sendmsg1(int fd,
                            const uv_buf_t* bufs,
                            unsigned int nbufs,
                            const struct sockaddr* addr);


void uv__udp_close(uv_udp_t* handle) {
  uv__io_close(handle->loop, &handle->io_watcher);
  uv__handle_stop(handle);

  if (handle->io_watcher.fd != -1) {
    uv__close(handle->io_watcher.fd);
    handle->io_watcher.fd = -1;
  }
}


void uv__udp_finish_close(uv_udp_t* handle) {
  uv_udp_send_t* req;
  struct uv__queue* q;

  assert(!uv__io_active(&handle->io_watcher, POLLIN | POLLOUT));
  assert(handle->io_watcher.fd == -1);

  while (!uv__queue_empty(&handle->write_queue)) {
    q = uv__queue_head(&handle->write_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp_send_t, queue);
    req->status = UV_ECANCELED;
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
  }

  uv__udp_run_completed(handle);

  assert(handle->send_queue_size == 0);
  assert(handle->send_queue_count == 0);

  /* Now tear down the handle. */
  handle->recv_cb = NULL;
  handle->alloc_cb = NULL;
  /* but _do not_ touch close_cb */
}


static void uv__udp_run_completed(uv_udp_t* handle) {
  uv_udp_send_t* req;
  struct uv__queue* q;

  assert(!(handle->flags & UV_HANDLE_UDP_PROCESSING));
  handle->flags |= UV_HANDLE_UDP_PROCESSING;

  while (!uv__queue_empty(&handle->write_completed_queue)) {
    q = uv__queue_head(&handle->write_completed_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp_send_t, queue);
    uv__req_unregister(handle->loop);

    handle->send_queue_size -= uv__count_bufs(req->bufs, req->nbufs);
    handle->send_queue_count--;

    if (req->bufs != req->bufsml)
      uv__free(req->bufs);
    req->bufs = NULL;

    if (req->send_cb == NULL)
      continue;

    /* req->status >= 0 == bytes written
     * req->status <  0 == errno
     */
    if (req->status >= 0)
      req->send_cb(req, 0);
    else
      req->send_cb(req, req->status);
  }

  if (uv__queue_empty(&handle->write_queue)) {
    /* Pending queue and completion queue empty, stop watcher. */
    uv__io_stop(handle->loop, &handle->io_watcher, POLLOUT);
    if (!uv__io_active(&handle->io_watcher, POLLIN))
      uv__handle_stop(handle);
  }

  handle->flags &= ~UV_HANDLE_UDP_PROCESSING;
}


#if defined(__linux__)
static int uv__udp_recvmsg_errqueue(uv_udp_t* handle,
                                    struct msghdr* h,
                                    uv_buf_t* buf,
                                    const struct sockaddr* peer,
                                    int flags) {
  struct cmsghdr* cmsg;
  struct sock_extended_err* serr;
  struct sockaddr* offender;

  if (!(h->msg_flags & MSG_ERRQUEUE))
    return 0;

  flags |= UV_UDP_LINUX_RECVERR;
  for (cmsg = CMSG_FIRSTHDR(h); cmsg != NULL; cmsg = CMSG_NXTHDR(h, cmsg)) {
    if ((cmsg->cmsg_level == SOL_IP   && cmsg->cmsg_type == IP_RECVERR) ||
        (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVERR)) {
      serr = (struct sock_extended_err*) CMSG_DATA(cmsg);

      offender = SO_EE_OFFENDER(serr);
      handle->recv_cb(handle,
                      UV__ERR(serr->ee_errno),
                      buf,
                      offender,
                      flags);
      return 1; /* handled */
    }
  }
  return 0;
}
#endif


void uv__udp_io(uv_loop_t* loop, uv__io_t* w, unsigned int revents) {
  uv_udp_t* handle;

  handle = container_of(w, uv_udp_t, io_watcher);
  assert(handle->type == UV_UDP);

  /* Trigger a recv and send to find out what POLLERR occurred. */
  if (revents & POLLERR)
    revents |= POLLIN | POLLOUT;

  if (revents & (POLLIN | POLLERR))
    uv__udp_recvmsg(handle, 0);

  /* Just Linux support for now. */
#if defined(__linux__)
  /* Guard against the case where the POLLIN callback above (e.g. via
   * uv_udp_recv_stop + uv_close) already cleared recv_cb in the same
   * revents iteration.  uv__udp_recvmsg asserts recv_cb != NULL, so
   * calling it with a NULL recv_cb would be wrong regardless of POLLERR. */
  if ((revents & POLLERR) && uv__is_active(handle) && handle->recv_cb != NULL)
    uv__udp_recvmsg(handle, MSG_ERRQUEUE);
#endif

  if (revents & POLLOUT && !uv__is_closing(handle)) {
    uv__udp_sendmsg(handle);
    uv__udp_run_completed(handle);
  }
}

static int uv__udp_recvmmsg(uv_udp_t* handle, uv_buf_t* buf, int flag) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  struct sockaddr_in6 peers[20];
  struct iovec iov[ARRAY_SIZE(peers)];
  struct mmsghdr msgs[ARRAY_SIZE(peers)];
  ssize_t nread;
  uv_buf_t chunk_buf;
  size_t chunks;
  int flags;
  size_t k;
#if defined(__linux__)
  char control[ARRAY_SIZE(peers)][64];
#endif

  /* prepare structures for recvmmsg */
  chunks = buf->len / UV__UDP_DGRAM_MAXSIZE;
  if (chunks == 0)
    return UV_EINVAL;
  if (chunks > ARRAY_SIZE(iov))
    chunks = ARRAY_SIZE(iov);
  for (k = 0; k < chunks; ++k) {
    iov[k].iov_base = buf->base + k * UV__UDP_DGRAM_MAXSIZE;
    iov[k].iov_len = UV__UDP_DGRAM_MAXSIZE;
    memset(&msgs[k].msg_hdr, 0, sizeof(msgs[k].msg_hdr));
    msgs[k].msg_hdr.msg_iov = iov + k;
    msgs[k].msg_hdr.msg_iovlen = 1;
    msgs[k].msg_hdr.msg_name = peers + k;
    msgs[k].msg_hdr.msg_namelen = sizeof(peers[0]);
    msgs[k].msg_hdr.msg_control = NULL;
    msgs[k].msg_hdr.msg_controllen = 0;
    msgs[k].msg_hdr.msg_flags = 0;
    msgs[k].msg_len = 0;
#if defined(__linux__)
    if (flag & MSG_ERRQUEUE) {
      msgs[k].msg_hdr.msg_control = control[k];
      msgs[k].msg_hdr.msg_controllen = sizeof(control[k]);
    }
#endif
  }

#if defined(__APPLE__)
  do
    nread = recvmsg_x(handle->io_watcher.fd, msgs, chunks, MSG_DONTWAIT);
  while (nread == -1 && errno == EINTR);
#else
  do
    nread = recvmmsg(handle->io_watcher.fd, msgs, chunks, flag, NULL);
  while (nread == -1 && errno == EINTR);
#endif

  if (nread < 1) {
    if (nread == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
      handle->recv_cb(handle, 0, buf, NULL, 0);
    else
      handle->recv_cb(handle, UV__ERR(errno), buf, NULL, 0);
  } else {
    /* pass each chunk to the application */
    for (k = 0; k < (size_t) nread && handle->recv_cb != NULL; k++) {
      flags = UV_UDP_MMSG_CHUNK;
      if (msgs[k].msg_hdr.msg_flags & MSG_TRUNC)
        flags |= UV_UDP_PARTIAL;

      chunk_buf = uv_buf_init(iov[k].iov_base, iov[k].iov_len);
#if defined(__linux__)
      if ((flag & MSG_ERRQUEUE) &&
          uv__udp_recvmsg_errqueue(handle, &msgs[k].msg_hdr, &chunk_buf,
                                   (const struct sockaddr*) &peers[k], flags)) {
        continue;
      }
#endif
      handle->recv_cb(handle,
                      msgs[k].msg_len,
                      &chunk_buf,
                      msgs[k].msg_hdr.msg_name,
                      flags);
    }

    /* one last callback so the original buffer is freed */
    if (handle->recv_cb != NULL)
      handle->recv_cb(handle, 0, buf, NULL, UV_UDP_MMSG_FREE);
  }
  return nread;
#else  /* __linux__ || ____FreeBSD__ || __APPLE__ */
  return UV_ENOSYS;
#endif  /* __linux__ || ____FreeBSD__ || __APPLE__ */
}

static void uv__udp_recvmsg(uv_udp_t* handle, int flag) {
  struct sockaddr_storage peer;
  struct msghdr h;
  ssize_t nread;
  uv_buf_t buf;
  int flags;
  int count;
#if defined(__linux__)
  char control[256];
#endif

  assert(handle->recv_cb != NULL);
  assert(handle->alloc_cb != NULL);

  /* Prevent loop starvation when the data comes in as fast as (or faster than)
   * we can read it. XXX Need to rearm fd if we switch to edge-triggered I/O.
   */
  count = 32;

  do {
    buf = uv_buf_init(NULL, 0);
    handle->alloc_cb((uv_handle_t*) handle, UV__UDP_DGRAM_MAXSIZE, &buf);
    if (buf.base == NULL || buf.len == 0) {
      handle->recv_cb(handle, UV_ENOBUFS, &buf, NULL, 0);
      return;
    }
    assert(buf.base != NULL);

    if (uv_udp_using_recvmmsg(handle)) {
      nread = uv__udp_recvmmsg(handle, &buf, flag);
      if (nread <= 0) {
        handle->recv_cb(handle, nread, &buf, NULL, 0);
        return;
      }
      count -= nread;
      continue;
    }

    memset(&h, 0, sizeof(h));
    memset(&peer, 0, sizeof(peer));
    h.msg_name = &peer;
    h.msg_namelen = sizeof(peer);
    h.msg_iov = (void*) &buf;
    h.msg_iovlen = 1;
#if defined(__linux__)
    if (flag & MSG_ERRQUEUE) {
      h.msg_control = control;
      h.msg_controllen = sizeof(control);
    }
#endif

    do
      nread = recvmsg(handle->io_watcher.fd, &h, flag);
    while (nread == -1 && errno == EINTR);

    flags = 0;
    if (nread != -1)
      if (h.msg_flags & MSG_TRUNC)
        flags |= UV_UDP_PARTIAL;

#if defined(__linux__)
    if ((flag & MSG_ERRQUEUE) &&
        uv__udp_recvmsg_errqueue(handle, &h, &buf, (void*) &peer, flags)) {
      count--;
      continue;
    }
#endif

    if (nread != -1)
      handle->recv_cb(handle, nread, &buf, (void*) &peer, flags);
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
      handle->recv_cb(handle, 0, &buf, NULL, 0);
    else
      handle->recv_cb(handle, UV__ERR(errno), &buf, NULL, 0);
    count--;
  }
  /* recv_cb callback may decide to pause or close the handle */
  while (nread != -1
      && count > 0
      && handle->io_watcher.fd != -1
      && handle->recv_cb != NULL);
}


/* On the BSDs, SO_REUSEPORT implies SO_REUSEADDR but with some additional
 * refinements for programs that use multicast. Therefore we preferentially
 * set SO_REUSEPORT over SO_REUSEADDR here, but we set SO_REUSEPORT only
 * when that socket option doesn't have the capability of load balancing.
 * Otherwise, we fall back to SO_REUSEADDR.
 *
 * Linux as of 3.9, DragonflyBSD 3.6, AIX 7.2.5 have the SO_REUSEPORT socket
 * option but with semantics that are different from the BSDs: it _shares_
 * the port rather than steals it from the current listener. While useful,
 * it's not something we can emulate on other platforms so we don't enable it.
 *
 * zOS does not support getsockname with SO_REUSEPORT option when using
 * AF_UNIX.
 *
 * Solaris 11.4: SO_REUSEPORT will not load balance when SO_REUSEADDR
 * is also set, but it's not valid for every socket type.
 */
static int uv__sock_reuseaddr(int fd) {
  int yes;
  yes = 1;

#if defined(SO_REUSEPORT) && defined(__MVS__)
  struct sockaddr_in sockfd;
  unsigned int sockfd_len = sizeof(sockfd);
  if (getsockname(fd, (struct sockaddr*) &sockfd, &sockfd_len) == -1)
      return UV__ERR(errno);
  if (sockfd.sin_family == AF_UNIX) {
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
      return UV__ERR(errno);
  } else {
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)))
       return UV__ERR(errno);
  }
#elif defined(SO_REUSEPORT) && defined(UV__SOLARIS_11_4) && UV__SOLARIS_11_4
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes))) {
    if (errno != ENOPROTOOPT)
      return UV__ERR(errno);
    /* Not all socket types accept SO_REUSEPORT. */
    errno = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
      return UV__ERR(errno);
  }
#elif defined(SO_REUSEPORT) && \
  !defined(__linux__) && !defined(__GNU__) && \
  !defined(__illumos__) && !defined(__DragonFly__) && !defined(_AIX73)
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)))
    return UV__ERR(errno);
#else
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
    return UV__ERR(errno);
#endif

  return 0;
}

/*
 * The Linux kernel suppresses some ICMP error messages by default for UDP
 * sockets. Setting IP_RECVERR/IPV6_RECVERR on the socket enables full ICMP
 * error reporting, hopefully resulting in faster failover to working name
 * servers.
 */
static int uv__set_recverr(int fd, sa_family_t ss_family) {
#if defined(__linux__)
  int yes;

  yes = 1;
  if (ss_family == AF_INET) {
    if (setsockopt(fd, IPPROTO_IP, IP_RECVERR, &yes, sizeof(yes)))
      return UV__ERR(errno);
  } else if (ss_family == AF_INET6) {
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVERR, &yes, sizeof(yes)))
       return UV__ERR(errno);
  }
#endif
  return 0;
}


int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;
  int yes;
  int fd;

  /* Check for bad flags. */
  if (flags & ~(UV_UDP_IPV6ONLY | UV_UDP_REUSEADDR |
                UV_UDP_REUSEPORT | UV_UDP_LINUX_RECVERR))
    return UV_EINVAL;

  /* Cannot set IPv6-only mode on non-IPv6 socket. */
  if ((flags & UV_UDP_IPV6ONLY) && addr->sa_family != AF_INET6)
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1) {
    err = uv__socket(addr->sa_family, SOCK_DGRAM, 0);
    if (err < 0)
      return err;
    fd = err;
    handle->io_watcher.fd = fd;
  }

  if (flags & UV_UDP_LINUX_RECVERR) {
    err = uv__set_recverr(fd, addr->sa_family);
    if (err)
      return err;
  }

  if (flags & UV_UDP_REUSEADDR) {
    err = uv__sock_reuseaddr(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP_REUSEPORT) {
    err = uv__sock_reuseport(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP_IPV6ONLY) {
#ifdef IPV6_V6ONLY
    yes = 1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof yes) == -1) {
      err = UV__ERR(errno);
      return err;
    }
#else
    err = UV_ENOTSUP;
    return err;
#endif
  }

  if (bind(fd, addr, addrlen)) {
    err = UV__ERR(errno);
    if (errno == EAFNOSUPPORT)
      /* OSX, other BSDs and SunoS fail with EAFNOSUPPORT when binding a
       * socket created with AF_INET to an AF_INET6 address or vice versa. */
      err = UV_EINVAL;
    return err;
  }

  if (addr->sa_family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;

  handle->flags |= UV_HANDLE_BOUND;
  return 0;
}


static int uv__udp_maybe_deferred_bind(uv_udp_t* handle,
                                       int domain,
                                       unsigned int flags) {
  union uv__sockaddr taddr;
  socklen_t addrlen;

  if (handle->io_watcher.fd != -1)
    return 0;

  switch (domain) {
  case AF_INET:
  {
    struct sockaddr_in* addr = &taddr.in;
    memset(addr, 0, sizeof *addr);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addrlen = sizeof *addr;
    break;
  }
  case AF_INET6:
  {
    struct sockaddr_in6* addr = &taddr.in6;
    memset(addr, 0, sizeof *addr);
    addr->sin6_family = AF_INET6;
    addr->sin6_addr = in6addr_any;
    addrlen = sizeof *addr;
    break;
  }
  default:
    assert(0 && "unsupported address family");
    abort();
  }

  return uv__udp_bind(handle, &taddr.addr, addrlen, flags);
}


int uv__udp_connect(uv_udp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen) {
  int err;

  err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
  if (err)
    return err;

  do {
    errno = 0;
    err = connect(handle->io_watcher.fd, addr, addrlen);
  } while (err == -1 && errno == EINTR);

  if (err)
    return UV__ERR(errno);

  handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}

/* From https://pubs.opengroup.org/onlinepubs/9699919799/functions/connect.html
 * Any of uv supported UNIXs kernel should be standardized, but the kernel
 * implementation logic not same, let's use pseudocode to explain the udp
 * disconnect behaviors:
 *
 * Predefined stubs for pseudocode:
 *   1. sodisconnect: The function to perform the real udp disconnect
 *   2. pru_connect: The function to perform the real udp connect
 *   3. so: The kernel object match with socket fd
 *   4. addr: The sockaddr parameter from user space
 *
 * BSDs:
 *   if(sodisconnect(so) == 0) { // udp disconnect succeed
 *     if (addr->sa_len != so->addr->sa_len) return EINVAL;
 *     if (addr->sa_family != so->addr->sa_family) return EAFNOSUPPORT;
 *     pru_connect(so);
 *   }
 *   else return EISCONN;
 *
 * z/OS (same with Windows):
 *   if(addr->sa_len < so->addr->sa_len) return EINVAL;
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 *
 * AIX:
 *   if(addr->sa_len != sizeof(struct sockaddr)) return EINVAL; // ignore ip proto version
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 *
 * Linux,Others:
 *   if(addr->sa_len < sizeof(struct sockaddr)) return EINVAL;
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 */
int uv__udp_disconnect(uv_udp_t* handle) {
    int r;
#if defined(__MVS__)
    struct sockaddr_storage addr;
#else
    struct sockaddr addr;
#endif

    memset(&addr, 0, sizeof(addr));

#if defined(__MVS__)
    addr.ss_family = AF_UNSPEC;
#else
    addr.sa_family = AF_UNSPEC;
#endif

    do {
      errno = 0;
#ifdef __PASE__
      /* On IBMi a connectionless transport socket can be disconnected by
       * either setting the addr parameter to NULL or setting the
       * addr_length parameter to zero, and issuing another connect().
       * https://www.ibm.com/docs/en/i/7.4?topic=ssw_ibm_i_74/apis/connec.htm
       */
      r = connect(handle->io_watcher.fd, (struct sockaddr*) NULL, 0);
#else
      r = connect(handle->io_watcher.fd, (struct sockaddr*) &addr, sizeof(addr));
#endif
    } while (r == -1 && errno == EINTR);

    if (r == -1) {
#if defined(BSD) || defined(__QNX__) /* The macro BSD is from sys/param.h */
      if (errno != EAFNOSUPPORT && errno != EINVAL)
        return UV__ERR(errno);
#else
      return UV__ERR(errno);
#endif
    }

    handle->flags &= ~UV_HANDLE_UDP_CONNECTED;
    return 0;
}

int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb) {
  int err;
  int empty_queue;

  assert(nbufs > 0);

  if (addr) {
    err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  }

  /* It's legal for send_queue_count > 0 even when the write_queue is empty;
   * it means there are error-state requests in the write_completed_queue that
   * will touch up send_queue_size/count later.
   */
  empty_queue = (handle->send_queue_count == 0);

  uv__req_init(handle->loop, req, UV_UDP_SEND);
  assert(addrlen <= sizeof(req->u.storage));
  if (addr == NULL)
    req->u.storage.ss_family = AF_UNSPEC;
  else
    memcpy(&req->u.storage, addr, addrlen);
  req->send_cb = send_cb;
  req->handle = handle;
  req->nbufs = nbufs;

  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(bufs[0]));

  if (req->bufs == NULL) {
    uv__req_unregister(handle->loop);
    return UV_ENOMEM;
  }

  memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
  handle->send_queue_size += uv__count_bufs(req->bufs, req->nbufs);
  handle->send_queue_count++;
  uv__queue_insert_tail(&handle->write_queue, &req->queue);
  uv__handle_start(handle);

  if (empty_queue && !(handle->flags & UV_HANDLE_UDP_PROCESSING)) {
    uv__udp_sendmsg(handle);

    /* `uv__udp_sendmsg` may not be able to do non-blocking write straight
     * away. In such cases the `io_watcher` has to be queued for asynchronous
     * write.
     */
    if (!uv__queue_empty(&handle->write_queue))
      uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  } else {
    uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  }

  return 0;
}


int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  int err;

  /* already sending a message */
  if (handle->send_queue_count != 0)
    return UV_EAGAIN;

  if (addr) {
    err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  } else {
    assert(handle->flags & UV_HANDLE_UDP_CONNECTED);
  }

  err = uv__udp_sendmsg1(handle->io_watcher.fd, bufs, nbufs, addr);
  if (err)
    return err;

  return uv__count_bufs(bufs, nbufs);
}


static int uv__udp_set_membership4(uv_udp_t* handle,
                                   const struct sockaddr_in* multicast_addr,
                                   const char* interface_addr,
                                   uv_membership membership) {
  struct ip_mreq mreq;
  int optname;
  int err;

  memset(&mreq, 0, sizeof mreq);

  if (interface_addr) {
    err = uv_inet_pton(AF_INET, interface_addr, &mreq.imr_interface.s_addr);
    if (err)
      return err;
  } else {
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  }

  mreq.imr_multiaddr.s_addr = multicast_addr->sin_addr.s_addr;

  switch (membership) {
  case UV_JOIN_GROUP:
    optname = IP_ADD_MEMBERSHIP;
    break;
  case UV_LEAVE_GROUP:
    optname = IP_DROP_MEMBERSHIP;
    break;
  default:
    return UV_EINVAL;
  }

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IP,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
#if defined(__MVS__)
  if (errno == ENXIO)
    return UV_ENODEV;
#endif
    return UV__ERR(errno);
  }

  return 0;
}


static int uv__udp_set_membership6(uv_udp_t* handle,
                                   const struct sockaddr_in6* multicast_addr,
                                   const char* interface_addr,
                                   uv_membership membership) {
  int optname;
  struct ipv6_mreq mreq;
  struct sockaddr_in6 addr6;

  memset(&mreq, 0, sizeof mreq);

  if (interface_addr) {
    if (uv_ip6_addr(interface_addr, 0, &addr6))
      return UV_EINVAL;
    mreq.ipv6mr_interface = addr6.sin6_scope_id;
  } else {
    mreq.ipv6mr_interface = 0;
  }

  mreq.ipv6mr_multiaddr = multicast_addr->sin6_addr;

  switch (membership) {
  case UV_JOIN_GROUP:
    optname = IPV6_ADD_MEMBERSHIP;
    break;
  case UV_LEAVE_GROUP:
    optname = IPV6_DROP_MEMBERSHIP;
    break;
  default:
    return UV_EINVAL;
  }

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IPV6,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
#if defined(__MVS__)
  if (errno == ENXIO)
    return UV_ENODEV;
#endif
    return UV__ERR(errno);
  }

  return 0;
}


#if !defined(__OpenBSD__) &&                                        \
    !defined(__NetBSD__) &&                                         \
    !defined(__ANDROID__) &&                                        \
    !defined(__DragonFly__) &&                                      \
    !defined(__GNU__) &&                                            \
    !defined(QNX_IOPKT)
static int uv__udp_set_source_membership4(uv_udp_t* handle,
                                          const struct sockaddr_in* multicast_addr,
                                          const char* interface_addr,
                                          const struct sockaddr_in* source_addr,
                                          uv_membership membership) {
  struct ip_mreq_source mreq;
  int optname;
  int err;

  err = uv__udp_maybe_deferred_bind(handle, AF_INET, UV_UDP_REUSEADDR);
  if (err)
    return err;

  memset(&mreq, 0, sizeof(mreq));

  if (interface_addr != NULL) {
    err = uv_inet_pton(AF_INET, interface_addr, &mreq.imr_interface.s_addr);
    if (err)
      return err;
  } else {
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  }

  mreq.imr_multiaddr.s_addr = multicast_addr->sin_addr.s_addr;
  mreq.imr_sourceaddr.s_addr = source_addr->sin_addr.s_addr;

  if (membership == UV_JOIN_GROUP)
    optname = IP_ADD_SOURCE_MEMBERSHIP;
  else if (membership == UV_LEAVE_GROUP)
    optname = IP_DROP_SOURCE_MEMBERSHIP;
  else
    return UV_EINVAL;

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IP,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
    return UV__ERR(errno);
  }

  return 0;
}


static int uv__udp_set_source_membership6(uv_udp_t* handle,
                                          const struct sockaddr_in6* multicast_addr,
                                          const char* interface_addr,
                                          const struct sockaddr_in6* source_addr,
                                          uv_membership membership) {
  struct group_source_req mreq;
  struct sockaddr_in6 addr6;
  int optname;
  int err;

  err = uv__udp_maybe_deferred_bind(handle, AF_INET6, UV_UDP_REUSEADDR);
  if (err)
    return err;

  memset(&mreq, 0, sizeof(mreq));

  if (interface_addr != NULL) {
    err = uv_ip6_addr(interface_addr, 0, &addr6);
    if (err)
      return err;
    mreq.gsr_interface = addr6.sin6_scope_id;
  } else {
    mreq.gsr_interface = 0;
  }

  STATIC_ASSERT(sizeof(mreq.gsr_group) >= sizeof(*multicast_addr));
  STATIC_ASSERT(sizeof(mreq.gsr_source) >= sizeof(*source_addr));
  memcpy(&mreq.gsr_group, multicast_addr, sizeof(*multicast_addr));
  memcpy(&mreq.gsr_source, source_addr, sizeof(*source_addr));

  if (membership == UV_JOIN_GROUP)
    optname = MCAST_JOIN_SOURCE_GROUP;
  else if (membership == UV_LEAVE_GROUP)
    optname = MCAST_LEAVE_SOURCE_GROUP;
  else
    return UV_EINVAL;

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IPV6,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
    return UV__ERR(errno);
  }

  return 0;
}
#endif


int uv__udp_init_ex(uv_loop_t* loop,
                    uv_udp_t* handle,
                    unsigned flags,
                    int domain) {
  int fd;

  fd = -1;
  if (domain != AF_UNSPEC) {
    fd = uv__socket(domain, SOCK_DGRAM, 0);
    if (fd < 0)
      return fd;
  }

  uv__handle_init(loop, (uv_handle_t*)handle, UV_UDP);
  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;
  handle->send_queue_size = 0;
  handle->send_queue_count = 0;
  uv__io_init(&handle->io_watcher, UV__UDP_IO, fd);
  uv__queue_init(&handle->write_queue);
  uv__queue_init(&handle->write_completed_queue);

  return 0;
}


int uv_udp_using_recvmmsg(const uv_udp_t* handle) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  if (handle->flags & UV_HANDLE_UDP_RECVMMSG)
    return 1;
#endif
  return 0;
}


int uv_udp_open_ex(uv_udp_t* handle, uv_os_sock_t sock, unsigned int flags) {
  int err;

  /* Check for bad flags. */
  if (flags & ~(UV_UDP_REUSEADDR | UV_UDP_REUSEPORT))
    return UV_EINVAL;

  /* Check for already active socket. */
  if (handle->io_watcher.fd != -1)
    return UV_EBUSY;

  if (uv__fd_exists(handle->loop, sock))
    return UV_EEXIST;

  err = uv__nonblock(sock, 1);
  if (err)
    return err;

  if (flags & UV_UDP_REUSEADDR) {
    err = uv__sock_reuseaddr(sock);
    if (err)
      return err;
  }

  if (flags & UV_UDP_REUSEPORT) {
    err = uv__sock_reuseport(sock);
    if (err)
      return err;
  }

  handle->io_watcher.fd = sock;
  if (uv__udp_is_connected(handle))
    handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}


int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock) {
  /*
   * Keep backward compatibility, always set REUSEADDR.
   * Refs: https://github.com/libuv/libuv/issues/4551
   */
  return uv_udp_open_ex(handle, sock, UV_UDP_REUSEADDR);
}


int uv_udp_set_membership(uv_udp_t* handle,
                          const char* multicast_addr,
                          const char* interface_addr,
                          uv_membership membership) {
  int err;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;

  if (uv_ip4_addr(multicast_addr, 0, &addr4) == 0) {
    err = uv__udp_maybe_deferred_bind(handle, AF_INET, UV_UDP_REUSEADDR);
    if (err)
      return err;
    return uv__udp_set_membership4(handle, &addr4, interface_addr, membership);
  } else if (uv_ip6_addr(multicast_addr, 0, &addr6) == 0) {
    err = uv__udp_maybe_deferred_bind(handle, AF_INET6, UV_UDP_REUSEADDR);
    if (err)
      return err;
    return uv__udp_set_membership6(handle, &addr6, interface_addr, membership);
  } else {
    return UV_EINVAL;
  }
}


int uv_udp_set_source_membership(uv_udp_t* handle,
                                 const char* multicast_addr,
                                 const char* interface_addr,
                                 const char* source_addr,
                                 uv_membership membership) {
#if !defined(__OpenBSD__) &&                                        \
    !defined(__NetBSD__) &&                                         \
    !defined(__ANDROID__) &&                                        \
    !defined(__DragonFly__) &&                                      \
    !defined(__GNU__) &&                                          \
    !defined(QNX_IOPKT)
  int err;
  union uv__sockaddr mcast_addr;
  union uv__sockaddr src_addr;

  err = uv_ip4_addr(multicast_addr, 0, &mcast_addr.in);
  if (err) {
    err = uv_ip6_addr(multicast_addr, 0, &mcast_addr.in6);
    if (err)
      return err;
    err = uv_ip6_addr(source_addr, 0, &src_addr.in6);
    if (err)
      return err;
    return uv__udp_set_source_membership6(handle,
                                          &mcast_addr.in6,
                                          interface_addr,
                                          &src_addr.in6,
                                          membership);
  }

  err = uv_ip4_addr(source_addr, 0, &src_addr.in);
  if (err)
    return err;
  return uv__udp_set_source_membership4(handle,
                                        &mcast_addr.in,
                                        interface_addr,
                                        &src_addr.in,
                                        membership);
#else
  return UV_ENOSYS;
#endif
}


static int uv__setsockopt(uv_udp_t* handle,
                         int option4,
                         int option6,
                         const void* val,
                         socklen_t size) {
  int r;

  if (handle->flags & UV_HANDLE_IPV6)
    r = setsockopt(handle->io_watcher.fd,
                   IPPROTO_IPV6,
                   option6,
                   val,
                   size);
  else
    r = setsockopt(handle->io_watcher.fd,
                   IPPROTO_IP,
                   option4,
                   val,
                   size);
  if (r)
    return UV__ERR(errno);

  return 0;
}

static int uv__setsockopt_maybe_char(uv_udp_t* handle,
                                     int option4,
                                     int option6,
                                     int val) {
#if defined(__sun) || defined(_AIX) || defined(__MVS__)
  char arg = val;
#elif defined(__OpenBSD__)
  unsigned char arg = val;
#else
  int arg = val;
#endif

  if (val < 0 || val > 255)
    return UV_EINVAL;

  return uv__setsockopt(handle, option4, option6, &arg, sizeof(arg));
}


int uv_udp_set_broadcast(uv_udp_t* handle, int on) {
  if (setsockopt(handle->io_watcher.fd,
                 SOL_SOCKET,
                 SO_BROADCAST,
                 &on,
                 sizeof(on))) {
    return UV__ERR(errno);
  }

  return 0;
}


int uv_udp_set_ttl(uv_udp_t* handle, int ttl) {
  if (ttl < 1 || ttl > 255)
    return UV_EINVAL;

#if defined(__MVS__)
  if (!(handle->flags & UV_HANDLE_IPV6))
    return UV_ENOTSUP;  /* zOS does not support setting ttl for IPv4 */
#endif

/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IP_TTL and IPV6_UNICAST_HOPS,
 * so hardcode the size of these options on this platform,
 * and use the general uv__setsockopt_maybe_char call on other platforms.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)

  return uv__setsockopt(handle,
                        IP_TTL,
                        IPV6_UNICAST_HOPS,
                        &ttl,
                        sizeof(ttl));

#else /* !(defined(__sun) || defined(_AIX) || defined (__OpenBSD__) ||
           defined(__MVS__) || defined(__QNX__)) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_TTL,
                                   IPV6_UNICAST_HOPS,
                                   ttl);

#endif /* defined(__sun) || defined(_AIX) || defined (__OpenBSD__) ||
          defined(__MVS__) || defined(__QNX__) */
}


int uv_udp_set_multicast_ttl(uv_udp_t* handle, int ttl) {
/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IPV6_MULTICAST_HOPS and sizeof(char) for
 * IP_MULTICAST_TTL, so hardcode the size of the option in the IPv6 case,
 * and use the general uv__setsockopt_maybe_char call otherwise.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)
  if (handle->flags & UV_HANDLE_IPV6)
    return uv__setsockopt(handle,
                          IP_MULTICAST_TTL,
                          IPV6_MULTICAST_HOPS,
                          &ttl,
                          sizeof(ttl));
#endif /* defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_MULTICAST_TTL,
                                   IPV6_MULTICAST_HOPS,
                                   ttl);
}


int uv_udp_set_multicast_loop(uv_udp_t* handle, int on) {
/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IPV6_MULTICAST_LOOP and sizeof(char) for
 * IP_MULTICAST_LOOP, so hardcode the size of the option in the IPv6 case,
 * and use the general uv__setsockopt_maybe_char call otherwise.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)
  if (handle->flags & UV_HANDLE_IPV6)
    return uv__setsockopt(handle,
                          IP_MULTICAST_LOOP,
                          IPV6_MULTICAST_LOOP,
                          &on,
                          sizeof(on));
#endif /* defined(__sun) || defined(_AIX) ||defined(__OpenBSD__) ||
    defined(__MVS__) || defined(__QNX__) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_MULTICAST_LOOP,
                                   IPV6_MULTICAST_LOOP,
                                   on);
}

int uv_udp_set_multicast_interface(uv_udp_t* handle, const char* interface_addr) {
  struct sockaddr_storage addr_st;
  struct sockaddr_in* addr4;
  struct sockaddr_in6* addr6;

  addr4 = (struct sockaddr_in*) &addr_st;
  addr6 = (struct sockaddr_in6*) &addr_st;

  if (!interface_addr) {
    memset(&addr_st, 0, sizeof addr_st);
    if (handle->flags & UV_HANDLE_IPV6) {
      addr_st.ss_family = AF_INET6;
      addr6->sin6_scope_id = 0;
    } else {
      addr_st.ss_family = AF_INET;
      addr4->sin_addr.s_addr = htonl(INADDR_ANY);
    }
  } else if (uv_ip4_addr(interface_addr, 0, addr4) == 0) {
    /* nothing, address was parsed */
  } else if (uv_ip6_addr(interface_addr, 0, addr6) == 0) {
    /* nothing, address was parsed */
  } else {
    return UV_EINVAL;
  }

  if (addr_st.ss_family == AF_INET) {
    if (setsockopt(handle->io_watcher.fd,
                   IPPROTO_IP,
                   IP_MULTICAST_IF,
                   (void*) &addr4->sin_addr,
                   sizeof(addr4->sin_addr)) == -1) {
      return UV__ERR(errno);
    }
  } else if (addr_st.ss_family == AF_INET6) {
    if (setsockopt(handle->io_watcher.fd,
                   IPPROTO_IPV6,
                   IPV6_MULTICAST_IF,
                   &addr6->sin6_scope_id,
                   sizeof(addr6->sin6_scope_id)) == -1) {
      return UV__ERR(errno);
    }
  } else {
    assert(0 && "unexpected address family");
    abort();
  }

  return 0;
}

int uv_udp_getpeername(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen);
}

int uv_udp_getsockname(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen);
}


int uv__udp_recv_start(uv_udp_t* handle,
                       uv_alloc_cb alloc_cb,
                       uv_udp_recv_cb recv_cb) {
  int err;

  if (alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;

  if (uv__io_active(&handle->io_watcher, POLLIN))
    return UV_EALREADY;  /* FIXME(bnoordhuis) Should be UV_EBUSY. */

  err = uv__udp_maybe_deferred_bind(handle, AF_INET, 0);
  if (err)
    return err;

  handle->alloc_cb = alloc_cb;
  handle->recv_cb = recv_cb;

  uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
  uv__handle_start(handle);

  return 0;
}


int uv__udp_recv_stop(uv_udp_t* handle) {
  uv__io_stop(handle->loop, &handle->io_watcher, POLLIN);

  if (!uv__io_active(&handle->io_watcher, POLLOUT))
    uv__handle_stop(handle);

  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;

  return 0;
}


static int uv__udp_prep_pkt(struct msghdr* h,
                            const uv_buf_t* bufs,
                            const unsigned int nbufs,
                            const struct sockaddr* addr) {
  memset(h, 0, sizeof(*h));
  h->msg_name = (void*) addr;
  h->msg_iov = (void*) bufs;
  h->msg_iovlen = nbufs;
  if (addr == NULL)
    return 0;
  switch (addr->sa_family) {
  case AF_INET:
    h->msg_namelen = sizeof(struct sockaddr_in);
    return 0;
  case AF_INET6:
    h->msg_namelen = sizeof(struct sockaddr_in6);
    return 0;
  case AF_UNIX:
    h->msg_namelen = sizeof(struct sockaddr_un);
    return 0;
  case AF_UNSPEC:
    h->msg_name = NULL;
    return 0;
  }
  return UV_EINVAL;
}


static int uv__udp_sendmsg1(int fd,
                            const uv_buf_t* bufs,
                            unsigned int nbufs,
                            const struct sockaddr* addr) {
  struct msghdr h;
  int r;

  if ((r = uv__udp_prep_pkt(&h, bufs, nbufs, addr)))
    return r;

  do
    r = sendmsg(fd, &h, 0);
  while (r == -1 && errno == EINTR);

  if (r < 0) {
    r = UV__ERR(errno);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
      r = UV_EAGAIN;
    return r;
  }

  /* UDP sockets don't EOF so we don't have to handle r=0 specially,
   * that only happens when the input was a zero-sized buffer.
   */
  return 0;
}


static int uv__udp_sendmsgv(int fd,
                            unsigned int count,
                            uv_buf_t* bufs[/*count*/],
                            unsigned int nbufs[/*count*/],
                            struct sockaddr* addrs[/*count*/]) {
  unsigned int i;
  int nsent;
  int r;

  r = 0;
  nsent = 0;

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) || \
  (defined(__sun__) && defined(MSG_WAITFORONE)) || defined(__QNX__)
  if (count > 1) {
    for (i = 0; i < count; /*empty*/) {
      struct mmsghdr m[20];
      unsigned int n;

      for (n = 0; i < count && n < ARRAY_SIZE(m); i++, n++)
        if ((r = uv__udp_prep_pkt(&m[n].msg_hdr, bufs[i], nbufs[i], addrs[i])))
          goto exit;

      do
#if defined(__APPLE__)
        r = sendmsg_x(fd, m, n, MSG_DONTWAIT);
#else
        r = sendmmsg(fd, m, n, 0);
#endif
      while (r == -1 && errno == EINTR);

      if (r < 1)
        goto exit;

      nsent += r;
      i += r;
    }

    goto exit;
  }
#endif  /* defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) ||
	 * (defined(__sun__) && defined(MSG_WAITFORONE)) || defined(__QNX__)
	 */

  for (i = 0; i < count; i++, nsent++)
    if ((r = uv__udp_sendmsg1(fd, bufs[i], nbufs[i], addrs[i])))
      goto exit;  /* goto to avoid unused label warning. */

exit:

  if (nsent > 0)
    return nsent;

  if (r < 0) {
    r = UV__ERR(errno);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
      r = UV_EAGAIN;
  }

  return r;
}


static void uv__udp_sendmsg(uv_udp_t* handle) {
  enum { N = 20 };
  struct sockaddr* addrs[N];
  unsigned int nbufs[N];
  uv_buf_t* bufs[N];
  struct uv__queue* q;
  uv_udp_send_t* req;
  int n;

  if (uv__queue_empty(&handle->write_queue))
    return;

again:
  n = 0;
  q = uv__queue_head(&handle->write_queue);
  do {
    req = uv__queue_data(q, uv_udp_send_t, queue);
    addrs[n] = &req->u.addr;
    nbufs[n] = req->nbufs;
    bufs[n] = req->bufs;
    q = uv__queue_next(q);
    n++;
  } while (n < N && q != &handle->write_queue);

  n = uv__udp_sendmsgv(handle->io_watcher.fd, n, bufs, nbufs, addrs);
  while (n > 0) {
    q = uv__queue_head(&handle->write_queue);
    req = uv__queue_data(q, uv_udp_send_t, queue);
    req->status = uv__count_bufs(req->bufs, req->nbufs);
    uv__queue_remove(&req->queue);
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
    n--;
  }

  if (n == 0) {
    if (uv__queue_empty(&handle->write_queue))
      goto feed;
    goto again;
  }

  if (n == UV_EAGAIN)
    return;

  /* Register the error against first request in queue because that
   * is the request that uv__udp_sendmsgv tried but failed to send,
   * because if it did send any requests, it won't return an error.
   */
  q = uv__queue_head(&handle->write_queue);
  req = uv__queue_data(q, uv_udp_send_t, queue);
  req->status = n;
  uv__queue_remove(&req->queue);
  uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
feed:
  uv__io_feed(handle->loop, &handle->io_watcher);
}


int uv__udp_try_send2(uv_udp_t* handle,
                      unsigned int count,
                      uv_buf_t* bufs[/*count*/],
                      unsigned int nbufs[/*count*/],
                      struct sockaddr* addrs[/*count*/]) {
  int fd;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EINVAL;

  return uv__udp_sendmsgv(fd, count, bufs, nbufs, addrs);
}


/*
 * uv_udp2_t implementation.
 */

/* Forward declarations. */
static void uv__udp2_run_completed(uv_udp2_t* handle);
static void uv__udp2_recvmsg(uv_udp2_t* handle, int flag);
static void uv__udp2_sendmsg(uv_udp2_t* handle);
static int uv__udp2_maybe_deferred_bind(uv_udp2_t* handle,
                                        int domain,
                                        unsigned int flags);

/* Enable ECN reception (IP_RECVTOS / IPV6_RECVTCLASS). */
static int uv__udp2_set_recvecn(int fd, sa_family_t family) {
  int yes = 1;
#if defined(IP_RECVTOS)
  if (family == AF_INET)
    setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &yes, sizeof(yes));
#endif
#if defined(IPV6_RECVTCLASS)
  if (family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_RECVTCLASS, &yes, sizeof(yes));
#endif
  return 0;
}


/* Enable Path MTU Discovery. mode: 0=off, 1=do, 2=probe. */
static int uv__udp2_set_pmtud(int fd, sa_family_t family, int mode) {
  int val;

#if defined(IP_MTU_DISCOVER)
  if (family == AF_INET) {
    if (mode == 0)
      val = IP_PMTUDISC_DONT;
    else if (mode == 1)
      val = IP_PMTUDISC_DO;
    else
      val = IP_PMTUDISC_PROBE;
    setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val));
  }
#if defined(IPV6_MTU_DISCOVER)
  if (family == AF_INET6) {
    if (mode == 0)
      val = IPV6_PMTUDISC_DONT;
    else if (mode == 1)
      val = IPV6_PMTUDISC_DO;
    else
      val = IPV6_PMTUDISC_PROBE;
    setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val, sizeof(val));
  }
#endif
#elif defined(IP_DONTFRAG)
  if (family == AF_INET) {
    val = (mode != 0);
    setsockopt(fd, IPPROTO_IP, IP_DONTFRAG, &val, sizeof(val));
  }
#endif
#if defined(IPV6_DONTFRAG) && !defined(IP_MTU_DISCOVER)
  if (family == AF_INET6) {
    val = (mode != 0);
    setsockopt(fd, IPPROTO_IPV6, IPV6_DONTFRAG, &val, sizeof(val));
  }
#endif
  return 0;
}


/* Enable pktinfo (local address + interface index reporting). */
static int uv__udp2_set_recvpktinfo(int fd, sa_family_t family) {
  int yes = 1;
  if (family == AF_INET) {
#if defined(IP_PKTINFO)
    setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &yes, sizeof(yes));
#elif defined(IP_RECVDSTADDR)
    setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, &yes, sizeof(yes));
#if defined(IP_RECVIF)
    setsockopt(fd, IPPROTO_IP, IP_RECVIF, &yes, sizeof(yes));
#endif
#endif
  }
#if defined(IPV6_RECVPKTINFO)
  if (family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &yes, sizeof(yes));
#endif
  return 0;
}


/* Parse the ECN codepoint from received ancillary data.
 * On macOS the cmsg_type for IPv4 TOS is IP_RECVTOS, not IP_TOS. */
static int uv__udp2_parse_ecn(const struct msghdr* h, sa_family_t family) {
  struct cmsghdr* cm;

  for (cm = CMSG_FIRSTHDR(h);
       cm != NULL;
       cm = CMSG_NXTHDR((struct msghdr*) h, cm)) {
    if (family == AF_INET && cm->cmsg_level == IPPROTO_IP) {
#if defined(__APPLE__)
      if (cm->cmsg_type == IP_RECVTOS)
#elif defined(IP_TOS)
      if (cm->cmsg_type == IP_TOS)
#else
      if (0)
#endif
        return *(uint8_t*) CMSG_DATA(cm) & 0x03;
    }
#if defined(IPV6_TCLASS)
    if (family == AF_INET6 &&
        cm->cmsg_level == IPPROTO_IPV6 &&
        cm->cmsg_type == IPV6_TCLASS &&
        cm->cmsg_len >= CMSG_LEN(sizeof(int))) {
      unsigned int tc;
      memcpy(&tc, CMSG_DATA(cm), sizeof(int));
      return tc & 0x03;
    }
#endif
  }
  return 0;
}


/* Parse pktinfo (local destination address + interface index).
 * Handles IP_PKTINFO (Linux) vs IP_RECVDSTADDR+IP_RECVIF (BSD/macOS). */
static void uv__udp2_parse_pktinfo(const struct msghdr* h,
                                   uv_udp2_recv_t* recv) {
  struct cmsghdr* cm;

  memset(&recv->local, 0, sizeof(recv->local));
  recv->ifindex = 0;

  for (cm = CMSG_FIRSTHDR(h);
       cm != NULL;
       cm = CMSG_NXTHDR((struct msghdr*) h, cm)) {
#if defined(IP_PKTINFO)
    if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO) {
      struct in_pktinfo pki;
      struct sockaddr_in* a;
      memcpy(&pki, CMSG_DATA(cm), sizeof(pki));
      a = (struct sockaddr_in*) &recv->local;
      a->sin_family = AF_INET;
      a->sin_addr = pki.ipi_addr;
      recv->ifindex = pki.ipi_ifindex;
    }
#elif defined(IP_RECVDSTADDR)
    if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_RECVDSTADDR) {
      struct sockaddr_in* a = (struct sockaddr_in*) &recv->local;
      a->sin_family = AF_INET;
      memcpy(&a->sin_addr, CMSG_DATA(cm), sizeof(struct in_addr));
    }
#if defined(IP_RECVIF)
    if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_RECVIF) {
      struct sockaddr_dl* sdl = (struct sockaddr_dl*) CMSG_DATA(cm);
      recv->ifindex = sdl->sdl_index;
    }
#endif
#endif
#if defined(IPV6_PKTINFO)
    if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
      struct in6_pktinfo pki;
      struct sockaddr_in6* a;
      memcpy(&pki, CMSG_DATA(cm), sizeof(pki));
      a = (struct sockaddr_in6*) &recv->local;
      a->sin6_family = AF_INET6;
      a->sin6_addr = pki.ipi6_addr;
      recv->ifindex = pki.ipi6_ifindex;
    }
#endif
  }
}


void uv__udp2_close(uv_udp2_t* handle) {
  uv__io_close(handle->loop, &handle->io_watcher);
  uv__handle_stop(handle);

  if (handle->io_watcher.fd != -1) {
    uv__close(handle->io_watcher.fd);
    handle->io_watcher.fd = -1;
  }
}


void uv__udp2_finish_close(uv_udp2_t* handle) {
  uv_udp2_send_t* req;
  struct uv__queue* q;

  assert(!uv__io_active(&handle->io_watcher, POLLIN | POLLOUT));
  assert(handle->io_watcher.fd == -1);

  while (!uv__queue_empty(&handle->write_queue)) {
    q = uv__queue_head(&handle->write_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp2_send_t, queue);
    req->status = UV_ECANCELED;
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
  }

  uv__udp2_run_completed(handle);

  assert(handle->send_queue_size == 0);
  assert(handle->send_queue_count == 0);

  /* Now tear down the handle. */
  handle->recv_cb = NULL;
  handle->alloc_cb = NULL;
  /* but _do not_ touch close_cb */
}


static void uv__udp2_run_completed(uv_udp2_t* handle) {
  uv_udp2_send_t* req;
  struct uv__queue* q;

  assert(!(handle->flags & UV_HANDLE_UDP2_PROCESSING));
  handle->flags |= UV_HANDLE_UDP2_PROCESSING;

  while (!uv__queue_empty(&handle->write_completed_queue)) {
    q = uv__queue_head(&handle->write_completed_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp2_send_t, queue);
    uv__req_unregister(handle->loop);

    handle->send_queue_size -= uv__count_bufs(req->bufs, req->nbufs);
    handle->send_queue_count--;

    if (req->bufs != req->bufsml)
      uv__free(req->bufs);
    req->bufs = NULL;

    if (req->send_cb == NULL)
      continue;

    /* req->status >= 0 == bytes written, req->status < 0 == errno. */
    if (req->status >= 0)
      req->send_cb(req, 0);
    else
      req->send_cb(req, req->status);
  }

  if (uv__queue_empty(&handle->write_queue)) {
    /* Pending queue and completion queue empty, stop watcher. */
    uv__io_stop(handle->loop, &handle->io_watcher, POLLOUT);
    if (!uv__io_active(&handle->io_watcher, POLLIN))
      uv__handle_stop(handle);
  }

  handle->flags &= ~UV_HANDLE_UDP2_PROCESSING;
}


#if defined(__linux__)
static int uv__udp2_recvmsg_errqueue(uv_udp2_t* handle,
                                     struct msghdr* h,
                                     const uv_buf_t* buf,
                                     const struct sockaddr* peer) {
  struct cmsghdr* cmsg;
  struct sock_extended_err* serr;
  struct sockaddr* offender;

  if (!(h->msg_flags & MSG_ERRQUEUE))
    return 0;

  for (cmsg = CMSG_FIRSTHDR(h);
       cmsg != NULL;
       cmsg = CMSG_NXTHDR(h, cmsg)) {
    if ((cmsg->cmsg_level == SOL_IP   && cmsg->cmsg_type == IP_RECVERR) ||
        (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVERR)) {
      uv_udp2_recv_t recv;

      serr = (struct sock_extended_err*) CMSG_DATA(cmsg);
      offender = SO_EE_OFFENDER(serr);

      memset(&recv, 0, sizeof(recv));
      recv.nread = UV__ERR(serr->ee_errno);
      recv.buf = buf;
      recv.addr = offender;
      recv.flags = UV_UDP2_RECV_LINUX_RECVERR;

      handle->recv_cb(handle, &recv);
      return 1; /* handled */
    }
  }
  return 0;
}
#endif


static unsigned int uv__udp2_parse_gro(const struct msghdr* h) {
#if defined(__linux__) && defined(UDP_GRO)
  struct cmsghdr* cm;
  for (cm = CMSG_FIRSTHDR(h); cm != NULL;
       cm = CMSG_NXTHDR((struct msghdr*) h, cm)) {
    if (cm->cmsg_level == SOL_UDP && cm->cmsg_type == UDP_SEGMENT) {
      uint16_t gso_size;
      memcpy(&gso_size, CMSG_DATA(cm), sizeof(gso_size));
      return (unsigned int) gso_size;
    }
  }
#endif
  return 0;
}


static int uv__udp2_recvmmsg(uv_udp2_t* handle, uv_buf_t* buf, int flag) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  struct sockaddr_in6 peers[20];
  struct iovec iov[ARRAY_SIZE(peers)];
  struct mmsghdr msgs[ARRAY_SIZE(peers)];
  ssize_t nread;
  uv_buf_t chunk_buf;
  size_t chunks;
  size_t k;
  int want_cmsg;
  char control[ARRAY_SIZE(peers)][256];

  /* Determine whether we need control message buffers. */
  want_cmsg = (handle->flags & (UV_HANDLE_UDP2_RECVECN |
                                UV_HANDLE_UDP2_RECVPKTINFO |
                                UV_HANDLE_UDP2_GRO |
                                UV_HANDLE_UDP2_GRO_RAW)) != 0;

#if defined(__linux__)
  /* Linux error queue always needs control buffers. */
  if (flag & MSG_ERRQUEUE)
    want_cmsg = 1;
#endif

  /* Prepare structures for recvmmsg. */
  chunks = buf->len / UV__UDP_DGRAM_MAXSIZE;
  if (chunks == 0)
    return UV_EINVAL;
  if (chunks > ARRAY_SIZE(iov))
    chunks = ARRAY_SIZE(iov);

  for (k = 0; k < chunks; ++k) {
    iov[k].iov_base = buf->base + k * UV__UDP_DGRAM_MAXSIZE;
    iov[k].iov_len = UV__UDP_DGRAM_MAXSIZE;
    memset(&msgs[k].msg_hdr, 0, sizeof(msgs[k].msg_hdr));
    msgs[k].msg_hdr.msg_iov = iov + k;
    msgs[k].msg_hdr.msg_iovlen = 1;
    msgs[k].msg_hdr.msg_name = peers + k;
    msgs[k].msg_hdr.msg_namelen = sizeof(peers[0]);
    msgs[k].msg_hdr.msg_flags = 0;
    msgs[k].msg_len = 0;
    if (want_cmsg) {
      msgs[k].msg_hdr.msg_control = control[k];
      msgs[k].msg_hdr.msg_controllen = sizeof(control[k]);
    } else {
      msgs[k].msg_hdr.msg_control = NULL;
      msgs[k].msg_hdr.msg_controllen = 0;
    }
  }

#if defined(__APPLE__)
  do
    nread = recvmsg_x(handle->io_watcher.fd, msgs, chunks, MSG_DONTWAIT);
  while (nread == -1 && errno == EINTR);
#else
  do
    nread = recvmmsg(handle->io_watcher.fd, msgs, chunks, flag, NULL);
  while (nread == -1 && errno == EINTR);
#endif

  if (nread < 1) {
    uv_udp2_recv_t recv;
    memset(&recv, 0, sizeof(recv));
    recv.buf = buf;
    if (nread == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
      recv.nread = 0;
    else
      recv.nread = UV__ERR(errno);
    handle->recv_cb(handle, &recv);
  } else {
    sa_family_t family;
    family = (handle->flags & UV_HANDLE_IPV6) ? AF_INET6 : AF_INET;

    /* Pass each chunk to the application. */
    for (k = 0; k < (size_t) nread && handle->recv_cb != NULL; k++) {
      uv_udp2_recv_t recv;
      int delivered = 0;

      memset(&recv, 0, sizeof(recv));
      recv.flags = UV_UDP2_MMSG_CHUNK;
      if (msgs[k].msg_hdr.msg_flags & MSG_TRUNC)
        recv.flags |= UV_UDP2_PARTIAL;

      chunk_buf = uv_buf_init(iov[k].iov_base, iov[k].iov_len);

#if defined(__linux__)
      if ((flag & MSG_ERRQUEUE) &&
          uv__udp2_recvmsg_errqueue(handle, &msgs[k].msg_hdr, &chunk_buf,
                                    (const struct sockaddr*) &peers[k])) {
        continue;
      }
#endif

      recv.nread = msgs[k].msg_len;
      recv.buf = &chunk_buf;
      recv.addr = (const struct sockaddr*) &peers[k];

      if (want_cmsg) {
        if (handle->flags & UV_HANDLE_UDP2_RECVECN)
          recv.ecn = uv__udp2_parse_ecn(&msgs[k].msg_hdr, family);
        if (handle->flags & UV_HANDLE_UDP2_RECVPKTINFO)
          uv__udp2_parse_pktinfo(&msgs[k].msg_hdr, &recv);
      }

      /* Check for GRO super-packet. */
      if (recv.nread > 0 && (handle->flags & (UV_HANDLE_UDP2_GRO |
                                              UV_HANDLE_UDP2_GRO_RAW))) {
        unsigned int seg = uv__udp2_parse_gro(&msgs[k].msg_hdr);
        if (seg > 0 && (unsigned int) recv.nread > seg) {
          if (handle->flags & UV_HANDLE_UDP2_GRO_RAW) {
            recv.segment_size = seg;
            handle->recv_cb(handle, &recv);
          } else {
            size_t off;
            uv_buf_t seg_buf;
            for (off = 0; off < (size_t) recv.nread; off += seg) {
              uv_udp2_recv_t seg_recv;
              size_t len;
              memset(&seg_recv, 0, sizeof(seg_recv));
              len = (size_t) recv.nread - off;
              if (len > seg)
                len = seg;
              seg_buf = uv_buf_init(chunk_buf.base + off, len);
              seg_recv.nread = len;
              seg_recv.buf = &seg_buf;
              seg_recv.addr = recv.addr;
              seg_recv.ecn = recv.ecn;
              seg_recv.local = recv.local;
              seg_recv.ifindex = recv.ifindex;
              seg_recv.segment_size = seg;
              seg_recv.flags = UV_UDP2_MMSG_CHUNK;
              handle->recv_cb(handle, &seg_recv);
              if (handle->recv_cb == NULL)
                break;
            }
          }
          delivered = 1;
        } else {
          recv.segment_size = 0;
        }
      }

      if (!delivered)
        handle->recv_cb(handle, &recv);
    }

    /* One last callback so the original buffer is freed. */
    if (handle->recv_cb != NULL) {
      uv_udp2_recv_t recv;
      memset(&recv, 0, sizeof(recv));
      recv.nread = 0;
      recv.buf = buf;
      recv.flags = UV_UDP2_MMSG_FREE;
      handle->recv_cb(handle, &recv);
    }
  }
  return nread;
#else  /* !(__linux__ || __FreeBSD__ || __APPLE__) */
  return UV_ENOSYS;
#endif
}


static void uv__udp2_recvmsg(uv_udp2_t* handle, int flag) {
  struct sockaddr_storage peer;
  struct msghdr h;
  ssize_t nread;
  uv_buf_t buf;
  int count;
  int want_cmsg;
  sa_family_t family;
  char control[256];

  assert(handle->recv_cb != NULL);
  assert(handle->alloc_cb != NULL);

  family = (handle->flags & UV_HANDLE_IPV6) ? AF_INET6 : AF_INET;
  want_cmsg = (handle->flags & (UV_HANDLE_UDP2_RECVECN |
                                UV_HANDLE_UDP2_RECVPKTINFO |
                                UV_HANDLE_UDP2_GRO |
                                UV_HANDLE_UDP2_GRO_RAW)) != 0;

#if defined(__linux__)
  if (flag & MSG_ERRQUEUE)
    want_cmsg = 1;
#endif

  /* Prevent loop starvation when data comes in as fast as we can read it. */
  count = 32;

  do {
    buf = uv_buf_init(NULL, 0);
    handle->alloc_cb(handle, UV__UDP_DGRAM_MAXSIZE, &buf);
    if (buf.base == NULL || buf.len == 0) {
      uv_udp2_recv_t recv;
      memset(&recv, 0, sizeof(recv));
      recv.nread = UV_ENOBUFS;
      recv.buf = &buf;
      handle->recv_cb(handle, &recv);
      return;
    }
    assert(buf.base != NULL);

    if (uv_udp2_using_recvmmsg(handle)) {
      nread = uv__udp2_recvmmsg(handle, &buf, flag);
      if (nread <= 0) {
        uv_udp2_recv_t recv;
        memset(&recv, 0, sizeof(recv));
        recv.nread = nread;
        recv.buf = &buf;
        handle->recv_cb(handle, &recv);
        return;
      }
      count -= nread;
      continue;
    }

    memset(&h, 0, sizeof(h));
    memset(&peer, 0, sizeof(peer));
    h.msg_name = &peer;
    h.msg_namelen = sizeof(peer);
    h.msg_iov = (void*) &buf;
    h.msg_iovlen = 1;

    if (want_cmsg) {
      h.msg_control = control;
      h.msg_controllen = sizeof(control);
    }

    do
      nread = recvmsg(handle->io_watcher.fd, &h, flag);
    while (nread == -1 && errno == EINTR);

#if defined(__linux__)
    if ((flag & MSG_ERRQUEUE) &&
        uv__udp2_recvmsg_errqueue(handle, &h, &buf,
                                  (const struct sockaddr*) &peer)) {
      count--;
      continue;
    }
#endif

    {
      uv_udp2_recv_t recv;
      int delivered = 0;
      memset(&recv, 0, sizeof(recv));
      recv.buf = &buf;
      recv.addr = (const struct sockaddr*) &peer;

      if (nread != -1) {
        recv.nread = nread;
        if (h.msg_flags & MSG_TRUNC)
          recv.flags |= UV_UDP2_PARTIAL;
        if (want_cmsg) {
          if (handle->flags & UV_HANDLE_UDP2_RECVECN)
            recv.ecn = uv__udp2_parse_ecn(&h, family);
          if (handle->flags & UV_HANDLE_UDP2_RECVPKTINFO)
            uv__udp2_parse_pktinfo(&h, &recv);
        }

        /* Check for GRO super-packet. */
        if (nread > 0 && (handle->flags & (UV_HANDLE_UDP2_GRO |
                                           UV_HANDLE_UDP2_GRO_RAW))) {
          unsigned int seg = uv__udp2_parse_gro(&h);
          if (seg > 0 && (unsigned int) nread > seg) {
            if (handle->flags & UV_HANDLE_UDP2_GRO_RAW) {
              /* Deliver the entire super-packet as-is. */
              recv.segment_size = seg;
              handle->recv_cb(handle, &recv);
            } else {
              /* Split into per-segment callbacks. */
              size_t off;
              uv_buf_t seg_buf;
              for (off = 0; off < (size_t) nread; off += seg) {
                uv_udp2_recv_t seg_recv;
                size_t len;
                memset(&seg_recv, 0, sizeof(seg_recv));
                len = (size_t) nread - off;
                if (len > seg)
                  len = seg;
                seg_buf = uv_buf_init(buf.base + off, len);
                seg_recv.nread = len;
                seg_recv.buf = &seg_buf;
                seg_recv.addr = recv.addr;
                seg_recv.ecn = recv.ecn;
                seg_recv.local = recv.local;
                seg_recv.ifindex = recv.ifindex;
                seg_recv.segment_size = seg;
                seg_recv.flags = UV_UDP2_MMSG_CHUNK;
                handle->recv_cb(handle, &seg_recv);
                if (handle->recv_cb == NULL)
                  break;
              }
              /* Final callback to release the buffer. */
              if (handle->recv_cb != NULL) {
                uv_udp2_recv_t free_recv;
                memset(&free_recv, 0, sizeof(free_recv));
                free_recv.buf = &buf;
                free_recv.flags = UV_UDP2_MMSG_FREE;
                handle->recv_cb(handle, &free_recv);
              }
            }
            delivered = 1;
          } else {
            recv.segment_size = 0;
          }
        }
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        recv.nread = 0;
        recv.addr = NULL;
      } else {
        recv.nread = UV__ERR(errno);
        recv.addr = NULL;
      }
      if (!delivered)
        handle->recv_cb(handle, &recv);
    }
    count--;
  }
  /* recv_cb callback may decide to pause or close the handle. */
  while (nread != -1
      && count > 0
      && handle->io_watcher.fd != -1
      && handle->recv_cb != NULL);
}


static void uv__udp2_sendmsg(uv_udp2_t* handle) {
  enum { N = 20 };
  struct sockaddr* addrs[N];
  unsigned int nbufs[N];
  uv_buf_t* bufs[N];
  struct uv__queue* q;
  uv_udp2_send_t* req;
  int n;

  if (uv__queue_empty(&handle->write_queue))
    return;

again:
  n = 0;
  q = uv__queue_head(&handle->write_queue);
  do {
    req = uv__queue_data(q, uv_udp2_send_t, queue);
    addrs[n] = &req->u.addr;
    nbufs[n] = req->nbufs;
    bufs[n] = req->bufs;
    q = uv__queue_next(q);
    n++;
  } while (n < N && q != &handle->write_queue);

  n = uv__udp_sendmsgv(handle->io_watcher.fd, n, bufs, nbufs, addrs);
  while (n > 0) {
    q = uv__queue_head(&handle->write_queue);
    req = uv__queue_data(q, uv_udp2_send_t, queue);
    req->status = uv__count_bufs(req->bufs, req->nbufs);
    uv__queue_remove(&req->queue);
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
    n--;
  }

  if (n == 0) {
    if (uv__queue_empty(&handle->write_queue))
      goto feed;
    goto again;
  }

  if (n == UV_EAGAIN)
    return;

  /* Register the error against first request in queue. */
  q = uv__queue_head(&handle->write_queue);
  req = uv__queue_data(q, uv_udp2_send_t, queue);
  req->status = n;
  uv__queue_remove(&req->queue);
  uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
feed:
  uv__io_feed(handle->loop, &handle->io_watcher);
}


void uv__udp2_io(uv_loop_t* loop, uv__io_t* w, unsigned int revents) {
  uv_udp2_t* handle;

  handle = container_of(w, uv_udp2_t, io_watcher);
  assert(handle->type == UV_UDP2);

  /* Trigger a recv and send to find out what POLLERR occurred. */
  if (revents & POLLERR)
    revents |= POLLIN | POLLOUT;

  if (revents & (POLLIN | POLLERR))
    uv__udp2_recvmsg(handle, 0);

#if defined(__linux__)
  if ((revents & POLLERR) && uv__is_active(handle))
    uv__udp2_recvmsg(handle, MSG_ERRQUEUE);
#endif

  if (revents & POLLOUT && !uv__is_closing(handle)) {
    uv__udp2_sendmsg(handle);
    uv__udp2_run_completed(handle);
  }
}


static int uv__udp2_maybe_deferred_bind(uv_udp2_t* handle,
                                        int domain,
                                        unsigned int flags) {
  union uv__sockaddr taddr;
  socklen_t addrlen;

  if (handle->io_watcher.fd != -1)
    return 0;

  switch (domain) {
  case AF_INET:
  {
    struct sockaddr_in* addr = &taddr.in;
    memset(addr, 0, sizeof *addr);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addrlen = sizeof *addr;
    break;
  }
  case AF_INET6:
  {
    struct sockaddr_in6* addr = &taddr.in6;
    memset(addr, 0, sizeof *addr);
    addr->sin6_family = AF_INET6;
    addr->sin6_addr = in6addr_any;
    addrlen = sizeof *addr;
    break;
  }
  default:
    assert(0 && "unsupported address family");
    abort();
  }

  return uv__udp2_bind(handle, &taddr.addr, addrlen, flags);
}


int uv__udp2_init_ex(uv_loop_t* loop,
                     uv_udp2_t* handle,
                     unsigned flags,
                     int domain) {
  int fd;

  fd = -1;
  if (domain != AF_UNSPEC) {
    fd = uv__socket(domain, SOCK_DGRAM, 0);
    if (fd < 0)
      return fd;
  }

  uv__handle_init(loop, (uv_handle_t*) handle, UV_UDP2);
  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;
  handle->send_queue_size = 0;
  handle->send_queue_count = 0;
  uv__io_init(&handle->io_watcher, UV__UDP2_IO, fd);
  uv__queue_init(&handle->write_queue);
  uv__queue_init(&handle->write_completed_queue);

  return 0;
}


int uv__udp2_bind(uv_udp2_t* handle,
                  const struct sockaddr* addr,
                  unsigned int addrlen,
                  unsigned int flags) {
  int err;
  int yes;
  int fd;

  /* Check for bad flags. */
  if (flags & ~(UV_UDP2_IPV6ONLY | UV_UDP2_REUSEADDR | UV_UDP2_REUSEPORT |
                UV_UDP2_LINUX_RECVERR | UV_UDP2_RECVMMSG | UV_UDP2_RECVECN |
                UV_UDP2_PMTUD | UV_UDP2_RECVPKTINFO |
                UV_UDP2_GRO | UV_UDP2_GRO_RAW | UV_UDP2_TXTIME))
    return UV_EINVAL;

  /* Cannot set IPv6-only mode on non-IPv6 socket. */
  if ((flags & UV_UDP2_IPV6ONLY) && addr->sa_family != AF_INET6)
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1) {
    err = uv__socket(addr->sa_family, SOCK_DGRAM, 0);
    if (err < 0)
      return err;
    fd = err;
    handle->io_watcher.fd = fd;
  }

  if (flags & UV_UDP2_LINUX_RECVERR) {
    err = uv__set_recverr(fd, addr->sa_family);
    if (err)
      return err;
  }

  if (flags & UV_UDP2_REUSEADDR) {
    err = uv__sock_reuseaddr(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP2_REUSEPORT) {
    err = uv__sock_reuseport(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP2_IPV6ONLY) {
#ifdef IPV6_V6ONLY
    yes = 1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof yes) == -1)
      return UV__ERR(errno);
#else
    return UV_ENOTSUP;
#endif
  }

  if (flags & UV_UDP2_RECVMMSG)
    handle->flags |= UV_HANDLE_UDP2_RECVMMSG;

  if (flags & UV_UDP2_RECVECN) {
    uv__udp2_set_recvecn(fd, addr->sa_family);
    handle->flags |= UV_HANDLE_UDP2_RECVECN;
  }

  if (flags & UV_UDP2_PMTUD) {
    /* Default to PROBE mode. */
    uv__udp2_set_pmtud(fd, addr->sa_family, 2);
  }

  if (flags & UV_UDP2_RECVPKTINFO) {
    uv__udp2_set_recvpktinfo(fd, addr->sa_family);
    handle->flags |= UV_HANDLE_UDP2_RECVPKTINFO;
  }

  if ((flags & UV_UDP2_GRO) && (flags & UV_UDP2_GRO_RAW))
    return UV_EINVAL;

  if (flags & (UV_UDP2_GRO | UV_UDP2_GRO_RAW)) {
#if defined(__linux__) && defined(UDP_GRO)
    int yes = 1;
    setsockopt(fd, SOL_UDP, UDP_GRO, &yes, sizeof(yes));
#endif
    if (flags & UV_UDP2_GRO)
      handle->flags |= UV_HANDLE_UDP2_GRO;
    else
      handle->flags |= UV_HANDLE_UDP2_GRO_RAW;
  }

  if (flags & UV_UDP2_TXTIME) {
#if defined(__linux__)
    struct uv__sock_txtime txtime_cfg;
    txtime_cfg.clockid = CLOCK_TAI;
    txtime_cfg.flags = 0;
    setsockopt(fd, SOL_SOCKET, SO_TXTIME, &txtime_cfg, sizeof(txtime_cfg));
#endif
  }

  if (bind(fd, addr, addrlen)) {
    err = UV__ERR(errno);
    if (errno == EAFNOSUPPORT)
      err = UV_EINVAL;
    return err;
  }

  if (addr->sa_family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;

  handle->flags |= UV_HANDLE_BOUND;
  return 0;
}


int uv__udp2_connect(uv_udp2_t* handle,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  int err;

  err = uv__udp2_maybe_deferred_bind(handle, addr->sa_family, 0);
  if (err)
    return err;

  do {
    errno = 0;
    err = connect(handle->io_watcher.fd, addr, addrlen);
  } while (err == -1 && errno == EINTR);

  if (err)
    return UV__ERR(errno);

  handle->flags |= UV_HANDLE_UDP2_CONNECTED;

  return 0;
}


int uv__udp2_disconnect(uv_udp2_t* handle) {
  int r;
#if defined(__MVS__)
  struct sockaddr_storage addr;
#else
  struct sockaddr addr;
#endif

  memset(&addr, 0, sizeof(addr));

#if defined(__MVS__)
  addr.ss_family = AF_UNSPEC;
#else
  addr.sa_family = AF_UNSPEC;
#endif

  do {
    errno = 0;
#ifdef __PASE__
    r = connect(handle->io_watcher.fd, (struct sockaddr*) NULL, 0);
#else
    r = connect(handle->io_watcher.fd, (struct sockaddr*) &addr, sizeof(addr));
#endif
  } while (r == -1 && errno == EINTR);

  if (r == -1) {
#if defined(BSD) || defined(__QNX__)
    if (errno != EAFNOSUPPORT && errno != EINVAL)
      return UV__ERR(errno);
#else
    return UV__ERR(errno);
#endif
  }

  handle->flags &= ~UV_HANDLE_UDP2_CONNECTED;
  return 0;
}


int uv__udp2_is_connected(uv_udp2_t* handle) {
  struct sockaddr_storage addr;
  socklen_t len;

  len = sizeof(addr);
  if (getpeername(handle->io_watcher.fd,
                  (struct sockaddr*) &addr, &len) != 0) {
    return 0;
  }

  return addr.ss_family != AF_UNSPEC;
}


int uv__udp2_send(uv_udp2_send_t* req,
                  uv_udp2_t* handle,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  const struct sockaddr* addr,
                  unsigned int addrlen,
                  uv_udp2_send_cb send_cb) {
  int err;
  int empty_queue;

  assert(nbufs > 0);

  if (addr) {
    err = uv__udp2_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  }

  empty_queue = (handle->send_queue_count == 0);

  uv__req_init(handle->loop, req, UV_UDP2_SEND);
  assert(addrlen <= sizeof(req->u.storage));
  if (addr == NULL)
    req->u.storage.ss_family = AF_UNSPEC;
  else
    memcpy(&req->u.storage, addr, addrlen);
  req->send_cb = send_cb;
  req->handle = handle;
  req->nbufs = nbufs;

  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(bufs[0]));

  if (req->bufs == NULL) {
    uv__req_unregister(handle->loop);
    return UV_ENOMEM;
  }

  memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
  handle->send_queue_size += uv__count_bufs(req->bufs, req->nbufs);
  handle->send_queue_count++;
  uv__queue_insert_tail(&handle->write_queue, &req->queue);
  uv__handle_start(handle);

  if (empty_queue && !(handle->flags & UV_HANDLE_UDP2_PROCESSING)) {
    uv__udp2_sendmsg(handle);

    if (!uv__queue_empty(&handle->write_queue))
      uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  } else {
    uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  }

  return 0;
}


int uv__udp2_try_send(uv_udp2_t* handle,
                      const uv_buf_t bufs[],
                      unsigned int nbufs,
                      const struct sockaddr* addr,
                      unsigned int addrlen) {
  int err;

  /* Already sending a message. */
  if (handle->send_queue_count != 0)
    return UV_EAGAIN;

  if (addr) {
    err = uv__udp2_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  } else {
    assert(handle->flags & UV_HANDLE_UDP2_CONNECTED);
  }

  err = uv__udp_sendmsg1(handle->io_watcher.fd, bufs, nbufs, addr);
  if (err)
    return err;

  return uv__count_bufs(bufs, nbufs);
}


int uv__udp2_try_send2(uv_udp2_t* handle,
                       unsigned int count,
                       uv_buf_t* bufs[/*count*/],
                       unsigned int nbufs[/*count*/],
                       struct sockaddr* addrs[/*count*/]) {
  int fd;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EINVAL;

  return uv__udp_sendmsgv(fd, count, bufs, nbufs, addrs);
}


int uv__udp2_recv_start(uv_udp2_t* handle,
                        uv_udp2_alloc_cb alloc_cb,
                        uv_udp2_recv_cb recv_cb) {
  int err;

  if (alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;

  if (uv__io_active(&handle->io_watcher, POLLIN))
    return UV_EALREADY;

  err = uv__udp2_maybe_deferred_bind(handle, AF_INET, 0);
  if (err)
    return err;

  handle->alloc_cb = alloc_cb;
  handle->recv_cb = recv_cb;

  uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
  uv__handle_start(handle);

  return 0;
}


int uv__udp2_recv_stop(uv_udp2_t* handle) {
  uv__io_stop(handle->loop, &handle->io_watcher, POLLIN);

  if (!uv__io_active(&handle->io_watcher, POLLOUT))
    uv__handle_stop(handle);

  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;

  return 0;
}


int uv_udp2_open(uv_udp2_t* handle, uv_os_sock_t sock) {
  int err;

  /* Check for already active socket. */
  if (handle->io_watcher.fd != -1)
    return UV_EBUSY;

  if (uv__fd_exists(handle->loop, sock))
    return UV_EEXIST;

  err = uv__nonblock(sock, 1);
  if (err)
    return err;

  handle->io_watcher.fd = sock;
  if (uv__udp2_is_connected(handle))
    handle->flags |= UV_HANDLE_UDP2_CONNECTED;

  return 0;
}


int uv_udp2_set_ecn(uv_udp2_t* handle, int ecn) {
  int fd;
  int r;

  if (ecn < 0 || ecn > 3)
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  if (handle->flags & UV_HANDLE_IPV6) {
#if defined(IPV6_TCLASS)
    r = setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &ecn, sizeof(ecn));
#else
    return UV_ENOTSUP;
#endif
  } else {
    /* Preserve DSCP bits, set only the low 2 (ECN) bits. */
    int tos = 0;
    socklen_t len = sizeof(tos);
    getsockopt(fd, IPPROTO_IP, IP_TOS, &tos, &len);
    tos = (tos & ~0x03) | (ecn & 0x03);
    r = setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
  }

  if (r)
    return UV__ERR(errno);

  return 0;
}


int uv_udp2_set_pmtud(uv_udp2_t* handle, enum uv_udp2_pmtud mode) {
  int fd;
  sa_family_t family;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  family = (handle->flags & UV_HANDLE_IPV6) ? AF_INET6 : AF_INET;
  return uv__udp2_set_pmtud(fd, family, (int) mode);
}


int uv_udp2_getmtu(const uv_udp2_t* handle, size_t* mtu) {
#if defined(IP_MTU)
  int fd;
  int val;
  socklen_t len;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  len = sizeof(val);
  if (handle->flags & UV_HANDLE_IPV6) {
#if defined(IPV6_MTU)
    if (getsockopt(fd, IPPROTO_IPV6, IPV6_MTU, &val, &len))
      return UV__ERR(errno);
#else
    return UV_ENOTSUP;
#endif
  } else {
    if (getsockopt(fd, IPPROTO_IP, IP_MTU, &val, &len))
      return UV__ERR(errno);
  }

  *mtu = (size_t) val;
  return 0;
#else
  return UV_ENOTSUP;
#endif
}


int uv_udp2_configure(uv_udp2_t* handle, unsigned int flags) {
  int fd;
  sa_family_t family;

  /* Only accept the configuration flags. */
  if (flags & ~(UV_UDP2_RECVECN | UV_UDP2_PMTUD |
                UV_UDP2_RECVPKTINFO | UV_UDP2_LINUX_RECVERR |
                UV_UDP2_GRO | UV_UDP2_GRO_RAW | UV_UDP2_TXTIME))
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  family = (handle->flags & UV_HANDLE_IPV6) ? AF_INET6 : AF_INET;

  if (flags & UV_UDP2_LINUX_RECVERR) {
    int err = uv__set_recverr(fd, family);
    if (err)
      return err;
  }

  if (flags & UV_UDP2_RECVECN) {
    uv__udp2_set_recvecn(fd, family);
    handle->flags |= UV_HANDLE_UDP2_RECVECN;
  }

  if (flags & UV_UDP2_PMTUD)
    uv__udp2_set_pmtud(fd, family, 2);

  if (flags & UV_UDP2_RECVPKTINFO) {
    uv__udp2_set_recvpktinfo(fd, family);
    handle->flags |= UV_HANDLE_UDP2_RECVPKTINFO;
  }

  if ((flags & UV_UDP2_GRO) && (flags & UV_UDP2_GRO_RAW))
    return UV_EINVAL;

  if (flags & (UV_UDP2_GRO | UV_UDP2_GRO_RAW)) {
#if defined(__linux__) && defined(UDP_GRO)
    int yes = 1;
    setsockopt(fd, SOL_UDP, UDP_GRO, &yes, sizeof(yes));
#endif
    if (flags & UV_UDP2_GRO)
      handle->flags |= UV_HANDLE_UDP2_GRO;
    else
      handle->flags |= UV_HANDLE_UDP2_GRO_RAW;
  }

  if (flags & UV_UDP2_TXTIME) {
#if defined(__linux__)
    struct uv__sock_txtime txtime_cfg;
    txtime_cfg.clockid = CLOCK_TAI;
    txtime_cfg.flags = 0;
    setsockopt(fd, SOL_SOCKET, SO_TXTIME, &txtime_cfg, sizeof(txtime_cfg));
#endif
  }

  return 0;
}


int uv_udp2_getsockname(const uv_udp2_t* handle,
                        struct sockaddr* name,
                        int* namelen) {
  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen);
}


int uv_udp2_getpeername(const uv_udp2_t* handle,
                        struct sockaddr* name,
                        int* namelen) {
  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen);
}


int uv_udp2_using_recvmmsg(const uv_udp2_t* handle) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  if (handle->flags & UV_HANDLE_UDP2_RECVMMSG)
    return 1;
#endif
  return 0;
}


int uv_udp2_set_ttl(uv_udp2_t* handle, int ttl) {
  int fd;
  int r;

  if (ttl < 1 || ttl > 255)
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

#if defined(__MVS__)
  if (!(handle->flags & UV_HANDLE_IPV6))
    return UV_ENOTSUP;
#endif

  if (handle->flags & UV_HANDLE_IPV6) {
    r = setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl));
  } else {
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)
    r = setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
#else
    /* Some platforms want a char-sized value. */
    unsigned char cttl = (unsigned char) ttl;
    r = setsockopt(fd, IPPROTO_IP, IP_TTL, &cttl, sizeof(cttl));
#endif
  }

  if (r)
    return UV__ERR(errno);

  return 0;
}


int uv_udp2_set_broadcast(uv_udp2_t* handle, int on) {
  int fd;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)))
    return UV__ERR(errno);

  return 0;
}


int uv_udp2_try_send_batch(uv_udp2_t* handle,
                            uv_udp2_mmsg_t* msgs,
                            unsigned int count) {
  int fd;
  unsigned int i;
  int has_gso;
  int has_txtime;
  int nsent;
  int r;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EINVAL;

  if (count == 0)
    return 0;

  has_gso = 0;
  for (i = 0; i < count; i++) {
    if (msgs[i].gso_size > 0) {
      has_gso = 1;
      break;
    }
  }

  has_txtime = 0;
  for (i = 0; i < count; i++) {
    if (msgs[i].txtime > 0) {
      has_txtime = 1;
      break;
    }
  }

#if defined(__linux__) && defined(UDP_SEGMENT)
  if (has_gso || has_txtime) {
    nsent = 0;
    for (i = 0; i < count; i++) {
      struct msghdr h;
      char ctrl[CMSG_SPACE(sizeof(uint16_t)) + CMSG_SPACE(sizeof(uint64_t))];

      r = uv__udp_prep_pkt(&h, msgs[i].bufs, msgs[i].nbufs, msgs[i].addr);
      if (r)
        return nsent > 0 ? nsent : r;

      memset(ctrl, 0, sizeof(ctrl));
      h.msg_control = NULL;
      h.msg_controllen = 0;

      if (msgs[i].gso_size > 0 || msgs[i].txtime > 0) {
        struct cmsghdr* cm;
        size_t ctrllen = 0;

        if (msgs[i].gso_size > 0)
          ctrllen += CMSG_SPACE(sizeof(uint16_t));
        if (msgs[i].txtime > 0)
          ctrllen += CMSG_SPACE(sizeof(uint64_t));

        h.msg_control = ctrl;
        h.msg_controllen = ctrllen;

        cm = CMSG_FIRSTHDR(&h);
        if (msgs[i].gso_size > 0) {
          uint16_t gso = (uint16_t) msgs[i].gso_size;
          cm->cmsg_level = SOL_UDP;
          cm->cmsg_type = UDP_SEGMENT;
          cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
          memcpy(CMSG_DATA(cm), &gso, sizeof(gso));
          cm = CMSG_NXTHDR(&h, cm);
        }
        if (msgs[i].txtime > 0 && cm != NULL) {
          cm->cmsg_level = SOL_SOCKET;
          cm->cmsg_type = SCM_TXTIME;
          cm->cmsg_len = CMSG_LEN(sizeof(uint64_t));
          memcpy(CMSG_DATA(cm), &msgs[i].txtime, sizeof(uint64_t));
        }
      }

      do
        r = sendmsg(fd, &h, 0);
      while (r == -1 && errno == EINTR);

      if (r < 0) {
        if (nsent > 0)
          return nsent;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
          return UV_EAGAIN;
        return UV__ERR(errno);
      }
      nsent++;
    }
    return nsent;
  }
#else
  (void) has_gso;
  (void) has_txtime;
#endif

  /* No GSO needed or not available; use standard sendmsgv. */
  {
    uv_buf_t* bufs_arr[20];
    unsigned int nbufs_arr[20];
    struct sockaddr* addrs_arr[20];

    nsent = 0;
    for (i = 0; i < count; /*empty*/) {
      unsigned int n;
      for (n = 0; i < count && n < ARRAY_SIZE(bufs_arr); i++, n++) {
        bufs_arr[n] = msgs[i].bufs;
        nbufs_arr[n] = msgs[i].nbufs;
        addrs_arr[n] = (struct sockaddr*) msgs[i].addr;
      }
      r = uv__udp_sendmsgv(fd, n, bufs_arr, nbufs_arr, addrs_arr);
      if (r < 0)
        return nsent > 0 ? nsent : r;
      nsent += r;
      if ((unsigned int) r < n)
        break;
    }
    return nsent;
  }
}


unsigned int uv_udp2_gso_max_segments(const uv_udp2_t* handle) {
#if defined(__linux__) && defined(UDP_SEGMENT)
  (void) handle;
  return 64;
#else
  (void) handle;
  return 0;
#endif
}


int uv_udp2_set_cpu_affinity(uv_udp2_t* handle, int cpu) {
#if defined(__linux__) && defined(SO_INCOMING_CPU)
  int fd;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EBADF;

  if (setsockopt(fd, SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu)))
    return UV__ERR(errno);

  return 0;
#else
  (void) handle;
  (void) cpu;
  return UV_ENOTSUP;
#endif
}
