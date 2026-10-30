/* Copyright libuv project contributors. All rights reserved.
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
#include "uv-common.h"

#if !defined(_WIN32)
# include <sys/un.h> /* AF_UNIX, sockaddr_un */
#endif

int uv_recv_buffer_size(uv_handle_t* handle, int* value) {
  return uv__socket_sockopt(handle, SO_RCVBUF, value);
}

int uv_send_buffer_size(uv_handle_t* handle, int *value) {
  return uv__socket_sockopt(handle, SO_SNDBUF, value);
}

int uv_tcp_bind(uv_tcp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;
  if (uv__is_closing(handle)) {
    return UV_EINVAL;
  }
  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_bind(handle, addr, addrlen, flags);
}


int uv_udp_init_ex(uv_loop_t* loop, uv_udp_t* handle, unsigned flags) {
  unsigned extra_flags;
  int domain;
  int rc;

  /* Use the lower 8 bits for the domain. */
  domain = flags & 0xFF;
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNSPEC)
    return UV_EINVAL;

  /* Use the higher bits for extra flags. */
  extra_flags = flags & ~0xFF;
  if (extra_flags & ~UV_UDP_RECVMMSG)
    return UV_EINVAL;

  rc = uv__udp_init_ex(loop, handle, flags, domain);

  if (rc == 0)
    if (extra_flags & UV_UDP_RECVMMSG)
      handle->flags |= UV_HANDLE_UDP_RECVMMSG;

  return rc;
}


int uv_udp_init(uv_loop_t* loop, uv_udp_t* handle) {
  return uv_udp_init_ex(loop, handle, AF_UNSPEC);
}


int uv_udp_bind(uv_udp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__udp_bind(handle, addr, addrlen, flags);
}


int uv_tcp_connect(uv_connect_t* req,
                   uv_tcp_t* handle,
                   const struct sockaddr* addr,
                   uv_connect_cb cb) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_connect(req, handle, addr, addrlen, cb);
}


int uv_udp_connect(uv_udp_t* handle, const struct sockaddr* addr) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  /* Disconnect the handle */
  if (addr == NULL) {
    if (!(handle->flags & UV_HANDLE_UDP_CONNECTED))
      return UV_ENOTCONN;

    return uv__udp_disconnect(handle);
  }

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  if (handle->flags & UV_HANDLE_UDP_CONNECTED)
    return UV_EISCONN;

  return uv__udp_connect(handle, addr, addrlen);
}


int uv__udp_is_connected(uv_udp_t* handle) {
  struct sockaddr_storage addr;
  int addrlen;
  if (handle->type != UV_UDP)
    return 0;

  addrlen = sizeof(addr);
  if (uv_udp_getpeername(handle, (struct sockaddr*) &addr, &addrlen) != 0)
    return 0;

  return addrlen > 0;
}


int uv__udp_check_before_send(uv_udp_t* handle,
                              const uv_buf_t bufs[],
                              unsigned int nbufs,
                              const struct sockaddr* addr) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr != NULL && (handle->flags & UV_HANDLE_UDP_CONNECTED))
    return UV_EISCONN;

  if (addr == NULL && !(handle->flags & UV_HANDLE_UDP_CONNECTED))
    return UV_EDESTADDRREQ;

  if (addr != NULL) {
    if (addr->sa_family == AF_INET)
      addrlen = sizeof(struct sockaddr_in);
    else if (addr->sa_family == AF_INET6)
      addrlen = sizeof(struct sockaddr_in6);
#if defined(AF_UNIX) && !defined(_WIN32)
    else if (addr->sa_family == AF_UNIX)
      addrlen = sizeof(struct sockaddr_un);
#endif
    else
      return UV_EINVAL;
  } else {
    addrlen = 0;
  }

  if (nbufs < 1 || nbufs > 1024 * 1024)
    return UV_EINVAL;

  if (uv__count_bufs(bufs, nbufs) > UV__IO_MAX_BYTES)
    return UV_EINVAL;

  return addrlen;
}


int uv_udp_send(uv_udp_send_t* req,
                uv_udp_t* handle,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                const struct sockaddr* addr,
                uv_udp_send_cb send_cb) {
  int addrlen;

  addrlen = uv__udp_check_before_send(handle, bufs, nbufs, addr);
  if (addrlen < 0)
    return addrlen;

  return uv__udp_send(req, handle, bufs, nbufs, addr, addrlen, send_cb);
}


int uv_udp_try_send(uv_udp_t* handle,
                    const uv_buf_t bufs[],
                    unsigned int nbufs,
                    const struct sockaddr* addr) {
  int addrlen;

  addrlen = uv__udp_check_before_send(handle, bufs, nbufs, addr);
  if (addrlen < 0)
    return addrlen;

  return uv__udp_try_send(handle, bufs, nbufs, addr, addrlen);
}


int uv_udp_try_send2(uv_udp_t* handle,
                     unsigned int count,
                     uv_buf_t* bufs[/*count*/],
                     unsigned int nbufs[/*count*/],
                     struct sockaddr* addrs[/*count*/],
                     unsigned int flags) {
  unsigned int i;
  int addrlen;

  if (count < 1)
    return UV_EINVAL;

  if (flags != 0)
    return UV_EINVAL;

  for (i = 0; i < count; i++) {
    addrlen = uv__udp_check_before_send(handle, bufs[i], nbufs[i], addrs[i]);
    if (addrlen < 0)
      return addrlen;
  }

  if (handle->send_queue_count > 0)
    return UV_EAGAIN;

  return uv__udp_try_send2(handle, count, bufs, nbufs, addrs);
}


int uv_udp_recv_start(uv_udp_t* handle,
                      uv_alloc_cb alloc_cb,
                      uv_udp_recv_cb recv_cb) {
  if (handle->type != UV_UDP || alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;
  else
    return uv__udp_recv_start(handle, alloc_cb, recv_cb);
}


int uv_udp_recv_stop(uv_udp_t* handle) {
  if (handle->type != UV_UDP)
    return UV_EINVAL;
  else
    return uv__udp_recv_stop(handle);
}
