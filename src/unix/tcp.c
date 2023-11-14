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

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#if 0

#if defined(__PASE__)
#include <as400_protos.h>
#define ifaddrs ifaddrs_pase
#define getifaddrs Qp2getifaddrs
#define freeifaddrs Qp2freeifaddrs
#else
#include <ifaddrs.h>
#endif

#endif

static int maybe_bind_socket(int fd) {
  union uv__sockaddr s;
  socklen_t slen;

  slen = sizeof(s);
  memset(&s, 0, sizeof(s));

  if (getsockname(fd, &s.addr, &slen))
    return UV__ERR(errno);

  if (s.addr.sa_family == AF_INET)
    if (s.in.sin_port != 0)
      return 0;  /* Already bound to a port. */

  if (s.addr.sa_family == AF_INET6)
    if (s.in6.sin6_port != 0)
      return 0;  /* Already bound to a port. */

  /* Bind to an arbitrary port. */
  if (bind(fd, &s.addr, slen))
    return UV__ERR(errno);

  return 0;
}


static int new_socket(uv_tcp_t* handle, int domain, unsigned int flags) {
  int sockfd;
  int err;

  sockfd = uv__socket(domain, SOCK_STREAM, 0);
  if (sockfd < 0)
    return sockfd;

  err = uv__stream_open((uv_stream_t*) handle, sockfd, flags);
  if (err) {
    uv__close(sockfd);
    return err;
  }

  if (flags & UV_HANDLE_BOUND)
    return maybe_bind_socket(sockfd);

  return 0;
}


static int maybe_new_socket(uv_tcp_t* handle, int domain, unsigned int flags) {
  int sockfd;
  int err;

  if (domain == AF_UNSPEC)
    goto out;

  sockfd = uv__stream_fd(handle);
  if (sockfd == -1)
    return new_socket(handle, domain, flags);

  if (!(flags & UV_HANDLE_BOUND))
    goto out;

  if (handle->flags & UV_HANDLE_BOUND)
    goto out;  /* Already bound to a port. */

  err = maybe_bind_socket(sockfd);
  if (err)
    return err;

out:

  handle->flags |= flags;
  return 0;
}


int uv_tcp_init_ex(uv_loop_t* loop, uv_tcp_t* tcp, unsigned int flags) {
  int domain;
  int err;

  /* Use the lower 8 bits for the domain */
  domain = flags & 0xFF;
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNSPEC)
    return UV_EINVAL;

  if (flags & ~0xFF)
    return UV_EINVAL;

  uv__stream_init(loop, (uv_stream_t*)tcp, UV_TCP);

  /* If anything fails beyond this point we need to remove the handle from
   * the handle queue, since it was added by uv__handle_init in uv_stream_init.
   */

  if (domain != AF_UNSPEC) {
    err = new_socket(tcp, domain, 0);
    if (err) {
      uv__queue_remove(&tcp->handle_queue);
      if (tcp->io_watcher.fd != -1)
        uv__close(tcp->io_watcher.fd);
      tcp->io_watcher.fd = -1;
      return err;
    }
  }

  return 0;
}


int uv_tcp_init(uv_loop_t* loop, uv_tcp_t* tcp) {
  return uv_tcp_init_ex(loop, tcp, AF_UNSPEC);
}


int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;
  int on;

  /* Cannot set IPv6-only mode on non-IPv6 socket. */
  if ((flags & UV_TCP_IPV6ONLY) && addr->sa_family != AF_INET6)
    return UV_EINVAL;

  err = maybe_new_socket(tcp, addr->sa_family, 0);
  if (err)
    return err;

  on = 1;
  if (setsockopt(tcp->io_watcher.fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
    return UV__ERR(errno);

#ifndef __OpenBSD__
#ifdef IPV6_V6ONLY
  if (addr->sa_family == AF_INET6) {
    on = (flags & UV_TCP_IPV6ONLY) != 0;
    if (setsockopt(tcp->io_watcher.fd,
                   IPPROTO_IPV6,
                   IPV6_V6ONLY,
                   &on,
                   sizeof on) == -1) {
#if defined(__MVS__)
      if (errno == EOPNOTSUPP)
        return UV_EINVAL;
#endif
      return UV__ERR(errno);
    }
  }
#endif
#endif

  errno = 0;
  err = bind(tcp->io_watcher.fd, addr, addrlen);
  if (err == -1 && errno != EADDRINUSE) {
    if (errno == EAFNOSUPPORT)
      /* OSX, other BSDs and SunoS fail with EAFNOSUPPORT when binding a
       * socket created with AF_INET to an AF_INET6 address or vice versa. */
      return UV_EINVAL;
    return UV__ERR(errno);
  }
  tcp->delayed_error = (err == -1) ? UV__ERR(errno) : 0;

  tcp->flags |= UV_HANDLE_BOUND;
  if (addr->sa_family == AF_INET6)
    tcp->flags |= UV_HANDLE_IPV6;

  return 0;
}


static int uv__is_ipv6_link_local(const struct sockaddr* addr) {
  const struct sockaddr_in6* a6;
  uint8_t b[2];

  if (addr->sa_family != AF_INET6)
    return 0;

  a6 = (const struct sockaddr_in6*) addr;
  memcpy(b, &a6->sin6_addr, sizeof(b));

  return b[0] == 0xFE && b[1] == 0x80;
}


static int uv__ipv6_link_local_scope_id(void) {
  struct sockaddr_in6* a6;
  int rv;
#if 0
  struct ifaddrs* ifa;
  struct ifaddrs* p;

  if (getifaddrs(&ifa))
    return 0;

  for (p = ifa; p != NULL; p = p->ifa_next)
    if (p->ifa_addr != NULL)
      if (uv__is_ipv6_link_local(p->ifa_addr))
        break;

  rv = 0;
  if (p != NULL) {
    a6 = (struct sockaddr_in6*) p->ifa_addr;
    rv = a6->sin6_scope_id;
  }

  freeifaddrs(ifa);
#else
  uv_interface_address_t* interfaces;
  int count, i;

  if (uv_interface_addresses(&interfaces, &count)) {
    return 0;
  }

  rv = 0;

  for (i = 0; i < count; i++) {
    if (uv__is_ipv6_link_local((const struct sockaddr*) &interfaces[i].address.address6)) {
      if (&interfaces[i].address.address6 != NULL) {
        a6 = &interfaces[i].address.address6;
        rv = a6->sin6_scope_id;
      }
      break;
    }
  }

  uv_free_interface_addresses(interfaces, count);
#endif

  return rv;
}


int uv__tcp_connect(uv_connect_t* req,
                    uv_tcp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_connect_cb cb) {
  struct sockaddr_in6 tmp6;
  int err;
  int r;

  assert(handle->type == UV_TCP);

  if (handle->connect_req != NULL)
    return UV_EALREADY;  /* FIXME(bnoordhuis) UV_EINVAL or maybe UV_EBUSY. */

  if (handle->delayed_error != 0)
    goto out;

  err = maybe_new_socket(handle,
                         addr->sa_family,
                         UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  if (err)
    return err;

  if (uv__is_ipv6_link_local(addr)) {
    memcpy(&tmp6, addr, sizeof(tmp6));
    if (tmp6.sin6_scope_id == 0) {
      tmp6.sin6_scope_id = uv__ipv6_link_local_scope_id();
      addr = (void*) &tmp6;
    }
  }

  do {
    errno = 0;
    r = connect(uv__stream_fd(handle), addr, addrlen);
  } while (r == -1 && errno == EINTR);

  /* We not only check the return value, but also check the errno != 0.
   * Because in rare cases connect() will return -1 but the errno
   * is 0 (for example, on Android 4.3, OnePlus phone A0001_12_150227)
   * and actually the tcp three-way handshake is completed.
   */
  if (r == -1 && errno != 0) {
    if (errno == EINPROGRESS)
      ; /* not an error */
    else if (errno == ECONNREFUSED
#if defined(__OpenBSD__)
      || errno == EINVAL
#endif
      )
    /* If we get ECONNREFUSED (Solaris) or EINVAL (OpenBSD) wait until the
     * next tick to report the error. Solaris and OpenBSD wants to report
     * immediately -- other unixes want to wait.
     */
      handle->delayed_error = UV__ERR(ECONNREFUSED);
    else
      return UV__ERR(errno);
  }

out:

  uv__req_init(handle->loop, req, UV_CONNECT);
  req->cb = cb;
  req->handle = (uv_stream_t*) handle;
  uv__queue_init(&req->queue);
  handle->connect_req = req;

  uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);

  if (handle->delayed_error)
    uv__io_feed(handle->loop, &handle->io_watcher);

  return 0;
}


int uv_tcp_open(uv_tcp_t* handle, uv_os_sock_t sock) {
  int err;

  if (uv__fd_exists(handle->loop, sock))
    return UV_EEXIST;

  err = uv__nonblock(sock, 1);
  if (err)
    return err;

  return uv__stream_open((uv_stream_t*)handle,
                         sock,
                         UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
}


int uv_tcp_getsockname(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  if (handle->delayed_error)
    return handle->delayed_error;

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen);
}


int uv_tcp_getpeername(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  if (handle->delayed_error)
    return handle->delayed_error;

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen);
}


int uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
  int fd;
  struct linger l = { 1, 0 };

  /* Disallow setting SO_LINGER to zero due to some platform inconsistencies */
  if (uv__is_stream_shutting(handle))
    return UV_EINVAL;

  fd = uv__stream_fd(handle);
  if (0 != setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l))) {
    if (errno == EINVAL) {
      /* Open Group Specifications Issue 7, 2018 edition states that
       * EINVAL may mean the socket has been shut down already.
       * Behavior observed on Solaris, illumos and macOS. */
      errno = 0;
    } else {
      return UV__ERR(errno);
    }
  }

  uv_close((uv_handle_t*) handle, close_cb);
  return 0;
}


int uv__tcp_listen(uv_tcp_t* tcp, int backlog, uv_connection_cb cb) {
  unsigned int flags;
  int err;

  if (tcp->delayed_error)
    return tcp->delayed_error;

  flags = 0;
#if defined(__MVS__)
  /* on zOS the listen call does not bind automatically
     if the socket is unbound. Hence the manual binding to
     an arbitrary port is required to be done manually
  */
  flags |= UV_HANDLE_BOUND;
#endif
  err = maybe_new_socket(tcp, AF_INET, flags);
  if (err)
    return err;

  if (listen(tcp->io_watcher.fd, backlog))
    return UV__ERR(errno);

  tcp->connection_cb = cb;
  tcp->flags |= UV_HANDLE_BOUND;

  /* Start listening for connections. */
  tcp->io_watcher.cb = uv__server_io;
  uv__io_start(tcp->loop, &tcp->io_watcher, POLLIN);

  return 0;
}


int uv__tcp_nodelay(int fd, int on) {
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)))
    return UV__ERR(errno);
  return 0;
}


int uv__tcp_keepalive(int fd, int on, unsigned int delay) {
  int intvl;
  int cnt;

  (void) &intvl;
  (void) &cnt;
    
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)))
    return UV__ERR(errno);

  if (!on)
    return 0;

#ifdef TCP_KEEPIDLE
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &delay, sizeof(delay)))
    return UV__ERR(errno);
/* Solaris/SmartOS, if you don't support keep-alive,
 * then don't advertise it in your system headers...
 */
/* FIXME(bnoordhuis) That's possibly because sizeof(delay) should be 1. */
#elif defined(TCP_KEEPALIVE) && !defined(__sun)
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &delay, sizeof(delay)))
    return UV__ERR(errno);
#endif

#ifdef TCP_KEEPINTVL
  intvl = 1;  /*  1 second; same as default on Win32 */
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)))
    return UV__ERR(errno);
#endif

#ifdef TCP_KEEPCNT
  cnt = 10;  /* 10 retries; same as hardcoded on Win32 */
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
    return UV__ERR(errno);
#endif

  return 0;
}


int uv_tcp_nodelay(uv_tcp_t* handle, int on) {
  int err;

  if (uv__stream_fd(handle) != -1) {
    err = uv__tcp_nodelay(uv__stream_fd(handle), on);
    if (err)
      return err;
  }

  if (on)
    handle->flags |= UV_HANDLE_TCP_NODELAY;
  else
    handle->flags &= ~UV_HANDLE_TCP_NODELAY;

  return 0;
}


int uv_tcp_keepalive(uv_tcp_t* handle, int on, unsigned int delay) {
  int err;

  if (uv__stream_fd(handle) != -1) {
    err =uv__tcp_keepalive(uv__stream_fd(handle), on, delay);
    if (err)
      return err;
  }

  if (on)
    handle->flags |= UV_HANDLE_TCP_KEEPALIVE;
  else
    handle->flags &= ~UV_HANDLE_TCP_KEEPALIVE;

  /* TODO Store delay if uv__stream_fd(handle) == -1 but don't want to enlarge
   *      uv_tcp_t with an int that's almost never used...
   */

  return 0;
}


int uv_tcp_simultaneous_accepts(uv_tcp_t* handle, int enable) {
  return 0;
}


void uv__tcp_close(uv_tcp_t* handle) {
  uv__stream_close((uv_stream_t*)handle);
}


int uv_socketpair(int type, int protocol, uv_os_sock_t fds[2], int flags0, int flags1) {
  uv_os_sock_t temp[2];
  int err;
#if defined(__FreeBSD__) || defined(__linux__)
  int flags;

  flags = type | SOCK_CLOEXEC;
  if ((flags0 & UV_NONBLOCK_PIPE) && (flags1 & UV_NONBLOCK_PIPE))
    flags |= SOCK_NONBLOCK;

  if (socketpair(AF_UNIX, flags, protocol, temp))
    return UV__ERR(errno);

  if (flags & UV_FS_O_NONBLOCK) {
    fds[0] = temp[0];
    fds[1] = temp[1];
    return 0;
  }
#else
  if (socketpair(AF_UNIX, type, protocol, temp))
    return UV__ERR(errno);

  if ((err = uv__cloexec(temp[0], 1)))
    goto fail;
  if ((err = uv__cloexec(temp[1], 1)))
    goto fail;
#endif

  if (flags0 & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[0], 1)))
        goto fail;
  if (flags1 & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[1], 1)))
      goto fail;

  fds[0] = temp[0];
  fds[1] = temp[1];
  return 0;

fail:
  uv__close(temp[0]);
  uv__close(temp[1]);
  return err;
}
