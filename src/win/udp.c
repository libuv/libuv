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

#include <assert.h>
#include <stdlib.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "stream-inl.h"
#include "req-inl.h"


/* A zero-size buffer for use by uv_udp_read */
static char uv_zero_[] = "";
int uv_udp_getpeername(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen,
                             0);
}


int uv_udp_getsockname(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen,
                             0);
}


static int uv__udp_set_socket(uv_loop_t* loop, uv_udp_t* handle, SOCKET socket,
    int family) {
  DWORD yes = 1;
  WSAPROTOCOL_INFOW info;
  int opt_len;

  if (handle->socket != INVALID_SOCKET)
    return UV_EBUSY;

  /* Set the socket to nonblocking mode */
  if (ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR) {
    return WSAGetLastError();
  }

  /* Associate it with the I/O completion port. Use uv_handle_t pointer as
   * completion key. */
  if (CreateIoCompletionPort((HANDLE)socket,
                             loop->iocp,
                             (ULONG_PTR)socket,
                             0) == NULL) {
    return GetLastError();
  }

  /* All known Windows that support SetFileCompletionNotificationModes have a
   * bug that makes it impossible to use this function in conjunction with
   * datagram sockets. We can work around that but only if the user is using
   * the default UDP driver (AFD) and has no other. LSPs stacked on top. Here
   * we check whether that is the case. */
  opt_len = (int) sizeof info;
  if (getsockopt(
          socket, SOL_SOCKET, SO_PROTOCOL_INFOW, (char*) &info, &opt_len) ==
      SOCKET_ERROR) {
    return GetLastError();
  }

  if (info.ProtocolChain.ChainLen == 1) {
    if (SetFileCompletionNotificationModes(
            (HANDLE) socket,
            FILE_SKIP_SET_EVENT_ON_HANDLE |
                FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
      handle->flags |= UV_HANDLE_SYNC_BYPASS_IOCP;
      handle->func_wsarecv = uv__wsarecv_workaround;
      handle->func_wsarecvfrom = uv__wsarecvfrom_workaround;
    } else if (GetLastError() != ERROR_INVALID_FUNCTION) {
      return GetLastError();
    }
  }

  handle->socket = socket;

  if (family == AF_INET6) {
    handle->flags |= UV_HANDLE_IPV6;
  } else {
    assert(!(handle->flags & UV_HANDLE_IPV6));
  }

  return 0;
}


int uv__udp_init_ex(uv_loop_t* loop,
                    uv_udp_t* handle,
                    unsigned flags,
                    int domain) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_UDP);
  handle->socket = INVALID_SOCKET;
  handle->reqs_pending = 0;
  handle->activecnt = 0;
  handle->func_wsarecv = WSARecv;
  handle->func_wsarecvfrom = WSARecvFrom;
  handle->send_queue_size = 0;
  handle->send_queue_count = 0;
  UV_REQ_INIT(&handle->recv_req, UV_UDP_RECV);
  handle->recv_req.data = handle;

  /* If anything fails beyond this point we need to remove the handle from
   * the handle queue, since it was added by uv__handle_init.
   */

  if (domain != AF_UNSPEC) {
    SOCKET sock;
    DWORD err;

    sock = WSASocketW(domain, SOCK_DGRAM, 0, NULL, 0,
                      WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (sock == INVALID_SOCKET) {
      err = WSAGetLastError();
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }

    err = uv__udp_set_socket(handle->loop, handle, sock, domain);
    if (err) {
      closesocket(sock);
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }
  }

  return 0;
}


void uv__udp_close(uv_loop_t* loop, uv_udp_t* handle) {
  uv_udp_recv_stop(handle);
  closesocket(handle->socket);
  handle->socket = INVALID_SOCKET;

  uv__handle_closing(handle);

  if (handle->reqs_pending == 0) {
    uv__want_endgame(loop, (uv_handle_t*) handle);
  }
}


void uv__udp_endgame(uv_loop_t* loop, uv_udp_t* handle) {
  if (handle->flags & UV_HANDLE_CLOSING &&
      handle->reqs_pending == 0) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    uv__handle_close(handle);
  }
}


int uv_udp_using_recvmmsg(const uv_udp_t* handle) {
  return 0;
}


static int uv__udp_maybe_bind(uv_udp_t* handle,
                              const struct sockaddr* addr,
                              unsigned int addrlen,
                              unsigned int flags) {
  int r;
  int err;
  DWORD no = 0;

  if (handle->flags & UV_HANDLE_BOUND)
    return 0;

  /* There is no SO_REUSEPORT on Windows, Windows only knows SO_REUSEADDR.
   * so we just return an error directly when UV_UDP_REUSEPORT is requested
   * for binding the socket. */
  if (flags & UV_UDP_REUSEPORT)
    return UV_ENOTSUP;

  if ((flags & UV_UDP_IPV6ONLY) && addr->sa_family != AF_INET6) {
    /* UV_UDP_IPV6ONLY is supported only for IPV6 sockets */
    return ERROR_INVALID_PARAMETER;
  }

  if (handle->socket == INVALID_SOCKET) {
    SOCKET sock = WSASocketW(addr->sa_family, SOCK_DGRAM, 0, NULL, 0,
                             WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (sock == INVALID_SOCKET) {
      return WSAGetLastError();
    }

    err = uv__udp_set_socket(handle->loop, handle, sock, addr->sa_family);
    if (err) {
      closesocket(sock);
      return err;
    }
  }

  if (flags & UV_UDP_REUSEADDR) {
    DWORD yes = 1;
    /* Set SO_REUSEADDR on the socket. */
    if (setsockopt(handle->socket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   (char*) &yes,
                   sizeof yes) == SOCKET_ERROR) {
      err = WSAGetLastError();
      return err;
    }
  }

  if (addr->sa_family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;

  if (addr->sa_family == AF_INET6 && !(flags & UV_UDP_IPV6ONLY)) {
    /* On windows IPV6ONLY is on by default. If the user doesn't specify it
     * libuv turns it off. */

    /* TODO: how to handle errors? This may fail if there is no ipv4 stack
     * available, or when run on XP/2003 which have no support for dualstack
     * sockets. For now we're silently ignoring the error. */
    setsockopt(handle->socket,
               IPPROTO_IPV6,
               IPV6_V6ONLY,
               (char*) &no,
               sizeof no);
  }

  r = bind(handle->socket, addr, addrlen);
  if (r == SOCKET_ERROR) {
    return WSAGetLastError();
  }

  handle->flags |= UV_HANDLE_BOUND;

  return 0;
}


static void uv__udp_queue_recv(uv_loop_t* loop, uv_udp_t* handle) {
  uv_req_t* req;
  uv_buf_t buf;
  DWORD bytes, flags;
  int result;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  req = &handle->recv_req;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  handle->flags |= UV_HANDLE_ZERO_READ;

  buf.base = (char*) uv_zero_;
  buf.len = 0;
  flags = MSG_PEEK;

  result = handle->func_wsarecv(handle->socket,
                                (WSABUF*) &buf,
                                1,
                                &bytes,
                                &flags,
                                &req->u.io.overlapped,
                                NULL);

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Process the req without IOCP. */
    handle->flags |= UV_HANDLE_READ_PENDING;
    req->u.io.overlapped.InternalHigh = bytes;
    handle->reqs_pending++;
    uv__insert_pending_req(loop, req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* The req will be processed with IOCP. */
    handle->flags |= UV_HANDLE_READ_PENDING;
    handle->reqs_pending++;
  } else {
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, req);
    handle->reqs_pending++;
  }
}


int uv__udp_recv_start(uv_udp_t* handle, uv_alloc_cb alloc_cb,
    uv_udp_recv_cb recv_cb) {
  uv_loop_t* loop = handle->loop;
  int err;

  if (handle->flags & UV_HANDLE_READING) {
    return UV_EALREADY;
  }

  err = uv__udp_maybe_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip4_any_,
                           sizeof(uv_addr_ip4_any_),
                           0);
  if (err)
    return uv_translate_sys_error(err);

  handle->flags |= UV_HANDLE_READING;
  INCREASE_ACTIVE_COUNT(loop, handle);

  handle->recv_cb = recv_cb;
  handle->alloc_cb = alloc_cb;

  /* If reading was stopped and then started again, there could still be a recv
   * request pending. */
  if (!(handle->flags & UV_HANDLE_READ_PENDING))
    uv__udp_queue_recv(loop, handle);

  return 0;
}


int uv__udp_recv_stop(uv_udp_t* handle) {
  if (handle->flags & UV_HANDLE_READING) {
    handle->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, handle);
  }

  return 0;
}


static int uv__send(uv_udp_send_t* req,
                    uv_udp_t* handle,
                    const uv_buf_t bufs[],
                    unsigned int nbufs,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_udp_send_cb cb) {
  uv_loop_t* loop = handle->loop;
  DWORD result, bytes;

  UV_REQ_INIT(req, UV_UDP_SEND);
  req->handle = handle;
  req->cb = cb;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  result = WSASendTo(handle->socket,
                     (WSABUF*)bufs,
                     nbufs,
                     &bytes,
                     0,
                     addr,
                     addrlen,
                     &req->u.io.overlapped,
                     NULL);

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Request completed immediately. */
    req->u.io.queued_bytes = 0;
    handle->reqs_pending++;
    handle->send_queue_size += req->u.io.queued_bytes;
    handle->send_queue_count++;
    REGISTER_HANDLE_REQ(loop, handle);
    uv__insert_pending_req(loop, (uv_req_t*)req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* Request queued by the kernel. */
    req->u.io.queued_bytes = uv__count_bufs(bufs, nbufs);
    handle->reqs_pending++;
    handle->send_queue_size += req->u.io.queued_bytes;
    handle->send_queue_count++;
    REGISTER_HANDLE_REQ(loop, handle);
  } else {
    /* Send failed due to an error. */
    return WSAGetLastError();
  }

  return 0;
}


void uv__process_udp_recv_req(uv_loop_t* loop, uv_udp_t* handle,
    uv_req_t* req) {
  uv_buf_t buf;
  int partial;

  assert(handle->type == UV_UDP);

  handle->flags &= ~UV_HANDLE_READ_PENDING;

  if (!REQ_SUCCESS(req)) {
    DWORD err = GET_REQ_SOCK_ERROR(req);
    if (err == WSAEMSGSIZE) {
      /* Not a real error, it just indicates that the received packet was
       * bigger than the receive buffer. */
    } else if (err == WSAECONNRESET || err == WSAENETRESET) {
      /* A previous sendto operation failed; ignore this error. If zero-reading
       * we need to call WSARecv/WSARecvFrom _without_ the. MSG_PEEK flag to
       * clear out the error queue. For nonzero reads, immediately queue a new
       * receive. */
      if (!(handle->flags & UV_HANDLE_ZERO_READ)) {
        goto done;
      }
    } else {
      /* A real error occurred. Report the error to the user only if we're
       * currently reading. */
      if (handle->flags & UV_HANDLE_READING) {
        uv_udp_recv_stop(handle);
        buf = (handle->flags & UV_HANDLE_ZERO_READ) ?
              uv_buf_init(NULL, 0) : handle->recv_buffer;
        handle->recv_cb(handle, uv_translate_sys_error(err), &buf, NULL, 0);
      }
      goto done;
    }
  }

  if (!(handle->flags & UV_HANDLE_ZERO_READ)) {
    /* Successful read */
    partial = !REQ_SUCCESS(req);
    handle->recv_cb(handle,
                    req->u.io.overlapped.InternalHigh,
                    &handle->recv_buffer,
                    (const struct sockaddr*) &handle->recv_from,
                    partial ? UV_UDP_PARTIAL : 0);
  } else if (handle->flags & UV_HANDLE_READING) {
    DWORD bytes, err, flags;
    struct sockaddr_storage from;
    int from_len;
    int count;

    /* Prevent loop starvation when the data comes in as fast as
     * (or faster than) we can read it. */
    count = 32;

    do {
      /* Do at most `count` nonblocking receive. */
      buf = uv_buf_init(NULL, 0);
      handle->alloc_cb((uv_handle_t*) handle, UV__UDP_DGRAM_MAXSIZE, &buf);
      if (buf.base == NULL || buf.len == 0) {
        handle->recv_cb(handle, UV_ENOBUFS, &buf, NULL, 0);
        goto done;
      }

      memset(&from, 0, sizeof from);
      from_len = sizeof from;

      flags = 0;

      if (WSARecvFrom(handle->socket,
                      (WSABUF*)&buf,
                      1,
                      &bytes,
                      &flags,
                      (struct sockaddr*) &from,
                      &from_len,
                      NULL,
                      NULL) != SOCKET_ERROR) {

        /* Message received */
        err = ERROR_SUCCESS;
        handle->recv_cb(handle, bytes, &buf, (const struct sockaddr*) &from, 0);
      } else {
        err = WSAGetLastError();
        if (err == WSAEMSGSIZE) {
          /* Message truncated */
          handle->recv_cb(handle,
                          bytes,
                          &buf,
                          (const struct sockaddr*) &from,
                          UV_UDP_PARTIAL);
        } else if (err == WSAEWOULDBLOCK) {
          /* Kernel buffer empty */
          handle->recv_cb(handle, 0, &buf, NULL, 0);
        } else if (err == WSAECONNRESET || err == WSAENETRESET) {
          /* WSAECONNRESET/WSANETRESET is ignored because this just indicates
           * that a previous sendto operation failed.
           */
          handle->recv_cb(handle, 0, &buf, NULL, 0);
        } else {
          /* Any other error that we want to report back to the user. */
          uv_udp_recv_stop(handle);
          handle->recv_cb(handle, uv_translate_sys_error(err), &buf, NULL, 0);
        }
      }
    }
    while (err == ERROR_SUCCESS &&
           count-- > 0 &&
           /* The recv_cb callback may decide to pause or close the handle. */
           (handle->flags & UV_HANDLE_READING) &&
           !(handle->flags & UV_HANDLE_READ_PENDING));
  }

done:
  /* Post another read if still reading and not closing. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__udp_queue_recv(loop, handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_udp_send_req(uv_loop_t* loop, uv_udp_t* handle,
    uv_udp_send_t* req) {
  int err;

  assert(handle->type == UV_UDP);

  assert(handle->send_queue_size >= req->u.io.queued_bytes);
  assert(handle->send_queue_count >= 1);
  handle->send_queue_size -= req->u.io.queued_bytes;
  handle->send_queue_count--;

  UNREGISTER_HANDLE_REQ(loop, handle);

  if (req->cb) {
    err = 0;
    if (!REQ_SUCCESS(req)) {
      err = GET_REQ_SOCK_ERROR(req);
    }
    req->cb(req, uv_translate_sys_error(err));
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


static int uv__udp_set_membership4(uv_udp_t* handle,
                                   const struct sockaddr_in* multicast_addr,
                                   const char* interface_addr,
                                   uv_membership membership) {
  int err;
  int optname;
  struct ip_mreq mreq;

  if (handle->flags & UV_HANDLE_IPV6)
    return UV_EINVAL;

  /* If the socket is unbound, bind to inaddr_any. */
  err = uv__udp_maybe_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip4_any_,
                           sizeof(uv_addr_ip4_any_),
                           UV_UDP_REUSEADDR);
  if (err)
    return uv_translate_sys_error(err);

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

  if (setsockopt(handle->socket,
                 IPPROTO_IP,
                 optname,
                 (char*) &mreq,
                 sizeof mreq) == SOCKET_ERROR) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


int uv__udp_set_membership6(uv_udp_t* handle,
                            const struct sockaddr_in6* multicast_addr,
                            const char* interface_addr,
                            uv_membership membership) {
  int optname;
  int err;
  struct ipv6_mreq mreq;
  struct sockaddr_in6 addr6;

  if ((handle->flags & UV_HANDLE_BOUND) && !(handle->flags & UV_HANDLE_IPV6))
    return UV_EINVAL;

  err = uv__udp_maybe_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip6_any_,
                           sizeof(uv_addr_ip6_any_),
                           UV_UDP_REUSEADDR);

  if (err)
    return uv_translate_sys_error(err);

  memset(&mreq, 0, sizeof(mreq));

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

  if (setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 optname,
                 (char*) &mreq,
                 sizeof mreq) == SOCKET_ERROR) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


static int uv__udp_set_source_membership4(uv_udp_t* handle,
                                          const struct sockaddr_in* multicast_addr,
                                          const char* interface_addr,
                                          const struct sockaddr_in* source_addr,
                                          uv_membership membership) {
  struct ip_mreq_source mreq;
  int optname;
  int err;

  if (handle->flags & UV_HANDLE_IPV6)
    return UV_EINVAL;

  /* If the socket is unbound, bind to inaddr_any. */
  err = uv__udp_maybe_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip4_any_,
                           sizeof(uv_addr_ip4_any_),
                           UV_UDP_REUSEADDR);
  if (err)
    return uv_translate_sys_error(err);

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

  if (setsockopt(handle->socket,
                 IPPROTO_IP,
                 optname,
                 (char*) &mreq,
                 sizeof(mreq)) == SOCKET_ERROR) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


int uv__udp_set_source_membership6(uv_udp_t* handle,
                                   const struct sockaddr_in6* multicast_addr,
                                   const char* interface_addr,
                                   const struct sockaddr_in6* source_addr,
                                   uv_membership membership) {
  struct group_source_req mreq;
  struct sockaddr_in6 addr6;
  int optname;
  int err;

  STATIC_ASSERT(sizeof(mreq.gsr_group) >= sizeof(*multicast_addr));
  STATIC_ASSERT(sizeof(mreq.gsr_source) >= sizeof(*source_addr));

  if ((handle->flags & UV_HANDLE_BOUND) && !(handle->flags & UV_HANDLE_IPV6))
    return UV_EINVAL;

  err = uv__udp_maybe_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip6_any_,
                           sizeof(uv_addr_ip6_any_),
                           UV_UDP_REUSEADDR);

  if (err)
    return uv_translate_sys_error(err);

  memset(&mreq, 0, sizeof(mreq));

  if (interface_addr != NULL) {
    err = uv_ip6_addr(interface_addr, 0, &addr6);
    if (err)
      return err;
    mreq.gsr_interface = addr6.sin6_scope_id;
  } else {
    mreq.gsr_interface = 0;
  }

  memcpy(&mreq.gsr_group, multicast_addr, sizeof(*multicast_addr));
  memcpy(&mreq.gsr_source, source_addr, sizeof(*source_addr));

  if (membership == UV_JOIN_GROUP)
    optname = MCAST_JOIN_SOURCE_GROUP;
  else if (membership == UV_LEAVE_GROUP)
    optname = MCAST_LEAVE_SOURCE_GROUP;
  else
    return UV_EINVAL;

  if (setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 optname,
                 (char*) &mreq,
                 sizeof(mreq)) == SOCKET_ERROR) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


int uv_udp_set_membership(uv_udp_t* handle,
                          const char* multicast_addr,
                          const char* interface_addr,
                          uv_membership membership) {
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;

  if (uv_ip4_addr(multicast_addr, 0, &addr4) == 0)
    return uv__udp_set_membership4(handle, &addr4, interface_addr, membership);
  else if (uv_ip6_addr(multicast_addr, 0, &addr6) == 0)
    return uv__udp_set_membership6(handle, &addr6, interface_addr, membership);
  else
    return UV_EINVAL;
}


int uv_udp_set_source_membership(uv_udp_t* handle,
                                 const char* multicast_addr,
                                 const char* interface_addr,
                                 const char* source_addr,
                                 uv_membership membership) {
  int err;
  struct sockaddr_storage mcast_addr;
  struct sockaddr_in* mcast_addr4;
  struct sockaddr_in6* mcast_addr6;
  struct sockaddr_storage src_addr;
  struct sockaddr_in* src_addr4;
  struct sockaddr_in6* src_addr6;

  mcast_addr4 = (struct sockaddr_in*)&mcast_addr;
  mcast_addr6 = (struct sockaddr_in6*)&mcast_addr;
  src_addr4 = (struct sockaddr_in*)&src_addr;
  src_addr6 = (struct sockaddr_in6*)&src_addr;

  err = uv_ip4_addr(multicast_addr, 0, mcast_addr4);
  if (err) {
    err = uv_ip6_addr(multicast_addr, 0, mcast_addr6);
    if (err)
      return err;
    err = uv_ip6_addr(source_addr, 0, src_addr6);
    if (err)
      return err;
    return uv__udp_set_source_membership6(handle,
                                          mcast_addr6,
                                          interface_addr,
                                          src_addr6,
                                          membership);
  }
  
  err = uv_ip4_addr(source_addr, 0, src_addr4);
  if (err)
    return err;
  return uv__udp_set_source_membership4(handle,
                                        mcast_addr4,
                                        interface_addr,
                                        src_addr4,
                                        membership);
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

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (addr_st.ss_family == AF_INET) {
    if (setsockopt(handle->socket,
                   IPPROTO_IP,
                   IP_MULTICAST_IF,
                   (char*) &addr4->sin_addr,
                   sizeof(addr4->sin_addr)) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  } else if (addr_st.ss_family == AF_INET6) {
    if (setsockopt(handle->socket,
                   IPPROTO_IPV6,
                   IPV6_MULTICAST_IF,
                   (char*) &addr6->sin6_scope_id,
                   sizeof(addr6->sin6_scope_id)) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  } else {
    assert(0 && "unexpected address family");
    abort();
  }

  return 0;
}


int uv_udp_set_broadcast(uv_udp_t* handle, int value) {
  BOOL optval = (BOOL) value;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (setsockopt(handle->socket,
                 SOL_SOCKET,
                 SO_BROADCAST,
                 (char*) &optval,
                 sizeof optval)) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


int uv__udp_is_bound(uv_udp_t* handle) {
  struct sockaddr_storage addr;
  int addrlen;

  addrlen = sizeof(addr);
  if (uv_udp_getsockname(handle, (struct sockaddr*) &addr, &addrlen) != 0)
    return 0;

  return addrlen > 0;
}


int uv_udp_open_ex(uv_udp_t* handle, uv_os_sock_t sock, unsigned int flags) {
  WSAPROTOCOL_INFOW protocol_info;
  int opt_len;
  int err;

  /* Check for bad flags. */
  if (flags & ~(UV_UDP_REUSEADDR | UV_UDP_REUSEPORT))
    return UV_EINVAL;

  /* Detect the address family of the socket. */
  opt_len = (int) sizeof protocol_info;
  if (getsockopt(sock,
                 SOL_SOCKET,
                 SO_PROTOCOL_INFOW,
                 (char*) &protocol_info,
                 &opt_len) == SOCKET_ERROR) {
    return uv_translate_sys_error(GetLastError());
  }

  err = uv__udp_set_socket(handle->loop,
                           handle,
                           sock,
                           protocol_info.iAddressFamily);
  if (err)
    return uv_translate_sys_error(err);

  /* There is no SO_REUSEPORT on Windows, Windows only knows SO_REUSEADDR.
   * so we just return an error directly when UV_UDP_REUSEPORT is requested
   * for binding the socket. */
  if (flags & UV_UDP_REUSEPORT)
    return UV_ENOTSUP;

  if (flags & UV_UDP_REUSEADDR) {
    DWORD yes = 1;
    /* Set SO_REUSEADDR on the socket. */
    if (setsockopt(handle->socket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   (char*) &yes,
                   sizeof yes) == SOCKET_ERROR) {
      err = WSAGetLastError();
      return uv_translate_sys_error(err);
    }
  }

  if (uv__udp_is_bound(handle))
    handle->flags |= UV_HANDLE_BOUND;

  if (uv__udp_is_connected(handle))
    handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}


int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock) {
  return uv_udp_open_ex(handle, sock, 0);
}


#define SOCKOPT_SETTER(name, option4, option6, validate)                      \
  int uv_udp_set_##name(uv_udp_t* handle, int value) {                        \
    DWORD optval = (DWORD) value;                                             \
                                                                              \
    if (!(validate(value))) {                                                 \
      return UV_EINVAL;                                                       \
    }                                                                         \
                                                                              \
    if (handle->socket == INVALID_SOCKET)                                     \
      return UV_EBADF;                                                        \
                                                                              \
    if (!(handle->flags & UV_HANDLE_IPV6)) {                                  \
      /* Set IPv4 socket option */                                            \
      if (setsockopt(handle->socket,                                          \
                     IPPROTO_IP,                                              \
                     option4,                                                 \
                     (char*) &optval,                                         \
                     sizeof optval)) {                                        \
        return uv_translate_sys_error(WSAGetLastError());                     \
      }                                                                       \
    } else {                                                                  \
      /* Set IPv6 socket option */                                            \
      if (setsockopt(handle->socket,                                          \
                     IPPROTO_IPV6,                                            \
                     option6,                                                 \
                     (char*) &optval,                                         \
                     sizeof optval)) {                                        \
        return uv_translate_sys_error(WSAGetLastError());                     \
      }                                                                       \
    }                                                                         \
    return 0;                                                                 \
  }

#define VALIDATE_TTL(value) ((value) >= 1 && (value) <= 255)
#define VALIDATE_MULTICAST_TTL(value) ((value) >= -1 && (value) <= 255)
#define VALIDATE_MULTICAST_LOOP(value) (1)

SOCKOPT_SETTER(ttl,
               IP_TTL,
               IPV6_HOPLIMIT,
               VALIDATE_TTL)
SOCKOPT_SETTER(multicast_ttl,
               IP_MULTICAST_TTL,
               IPV6_MULTICAST_HOPS,
               VALIDATE_MULTICAST_TTL)
SOCKOPT_SETTER(multicast_loop,
               IP_MULTICAST_LOOP,
               IPV6_MULTICAST_LOOP,
               VALIDATE_MULTICAST_LOOP)

#undef SOCKOPT_SETTER
#undef VALIDATE_TTL
#undef VALIDATE_MULTICAST_TTL
#undef VALIDATE_MULTICAST_LOOP


/* This function is an egress point, i.e. it returns libuv errors rather than
 * system errors.
 */
int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;

  err = uv__udp_maybe_bind(handle, addr, addrlen, flags);
  if (err)
    return uv_translate_sys_error(err);

  return 0;
}


int uv__udp_connect(uv_udp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen) {
  const struct sockaddr* bind_addr;
  int err;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;

    err = uv__udp_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = connect(handle->socket, addr, addrlen);
  if (err)
    return uv_translate_sys_error(WSAGetLastError());

  handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}


int uv__udp_disconnect(uv_udp_t* handle) {
    int err;
    struct sockaddr_storage addr;

    memset(&addr, 0, sizeof(addr));

    err = connect(handle->socket, (struct sockaddr*) &addr, sizeof(addr));
    if (err)
      return uv_translate_sys_error(WSAGetLastError());

    handle->flags &= ~UV_HANDLE_UDP_CONNECTED;
    return 0;
}


/* This function is an egress point, i.e. it returns libuv errors rather than
 * system errors.
 */
int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb) {
  const struct sockaddr* bind_addr;
  int err;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;

    err = uv__udp_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = uv__send(req, handle, bufs, nbufs, addr, addrlen, send_cb);
  if (err)
    return uv_translate_sys_error(err);

  return 0;
}


int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  DWORD bytes;
  const struct sockaddr* bind_addr;
  struct sockaddr_storage converted;
  int err;

  if (addr != NULL) {
    err = uv__convert_to_localhost_if_unspecified(addr, &converted);
    if (err)
      return err;
    addr = (const struct sockaddr*) &converted;
  }

  /* Already sending a message.*/
  if (handle->send_queue_count != 0)
    return UV_EAGAIN;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;
    err = uv__udp_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = WSASendTo(handle->socket,
                  (WSABUF*)bufs,
                  nbufs,
                  &bytes,
                  0,
                  addr,
                  addrlen,
                  NULL,
                  NULL);

  if (err)
    return uv_translate_sys_error(WSAGetLastError());

  return bytes;
}


int uv__udp_try_send2(uv_udp_t* handle,
                      unsigned int count,
                      uv_buf_t* bufs[/*count*/],
                      unsigned int nbufs[/*count*/],
                      struct sockaddr* addrs[/*count*/]) {
  int i;
  int r;

  if (count > INT_MAX)
    return UV_EINVAL;

  for (i = 0; i < (int) count; i++) {
    r = uv_udp_try_send(handle, bufs[i], nbufs[i], addrs[i]);
    if (r < 0)
      return i > 0 ? i : (int) r;  /* Error if first packet, else send count. */
  }

  return i;
}


/*
 * uv_udp2_t implementation (Windows)
 */

/* WSARecvMsg GUID: SIO_GET_EXTENSION_FUNCTION_POINTER for WSARecvMsg. */
static const GUID uv__wsarecvmsg_guid = {
  0xf689d7c8, 0x6f1f, 0x436b,
  { 0x8a, 0x53, 0xe5, 0x4f, 0xe3, 0x51, 0xc3, 0x22 }
};


static int uv__udp2_set_socket(uv_loop_t* loop, uv_udp2_t* handle,
    SOCKET socket, int family) {
  DWORD yes = 1;
  WSAPROTOCOL_INFOW info;
  int opt_len;

  if (handle->socket != INVALID_SOCKET)
    return UV_EBUSY;

  /* Set the socket to nonblocking mode. */
  if (ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR)
    return WSAGetLastError();

  /* Associate it with the I/O completion port. */
  if (CreateIoCompletionPort((HANDLE) socket,
                             loop->iocp,
                             (ULONG_PTR) socket,
                             0) == NULL) {
    return GetLastError();
  }

  /* Check whether we can use SetFileCompletionNotificationModes. */
  opt_len = (int) sizeof info;
  if (getsockopt(socket,
                 SOL_SOCKET,
                 SO_PROTOCOL_INFOW,
                 (char*) &info,
                 &opt_len) == SOCKET_ERROR) {
    return GetLastError();
  }

  if (info.ProtocolChain.ChainLen == 1) {
    if (SetFileCompletionNotificationModes(
            (HANDLE) socket,
            FILE_SKIP_SET_EVENT_ON_HANDLE |
                FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
      handle->flags |= UV_HANDLE_SYNC_BYPASS_IOCP;
      handle->func_wsarecv = uv__wsarecv_workaround;
      handle->func_wsarecvfrom = uv__wsarecvfrom_workaround;
    } else if (GetLastError() != ERROR_INVALID_FUNCTION) {
      return GetLastError();
    }
  }

  handle->socket = socket;

  if (family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;
  else
    assert(!(handle->flags & UV_HANDLE_IPV6));

  /* Obtain WSARecvMsg function pointer if ECN or pktinfo is requested. */
  if (handle->flags & (UV_HANDLE_UDP2_RECVECN | UV_HANDLE_UDP2_RECVPKTINFO)) {
    DWORD bytes;
    LPFN_WSARECVMSG func = NULL;
    if (WSAIoctl(socket,
                 SIO_GET_EXTENSION_FUNCTION_POINTER,
                 (void*) &uv__wsarecvmsg_guid,
                 sizeof(uv__wsarecvmsg_guid),
                 &func,
                 sizeof(func),
                 &bytes,
                 NULL,
                 NULL) != SOCKET_ERROR) {
      handle->func_wsarecvmsg = func;
    }
    /* If WSAIoctl fails, func_wsarecvmsg stays NULL. We still set the
     * socket options so the kernel collects the data, but we won't be
     * able to read it via WSARecvMsg until the recv path is wired up. */
  }

  return 0;
}


int uv__udp2_init_ex(uv_loop_t* loop,
                     uv_udp2_t* handle,
                     unsigned flags,
                     int domain) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_UDP2);
  handle->socket = INVALID_SOCKET;
  handle->reqs_pending = 0;
  handle->activecnt = 0;
  handle->func_wsarecv = WSARecv;
  handle->func_wsarecvfrom = WSARecvFrom;
  handle->func_wsarecvmsg = NULL;
  handle->send_queue_size = 0;
  handle->send_queue_count = 0;
  UV_REQ_INIT(&handle->recv_req, UV_UDP_RECV);
  handle->recv_req.data = handle;

  /* If anything fails beyond this point we need to remove the handle from
   * the handle queue, since it was added by uv__handle_init. */

  if (domain != AF_UNSPEC) {
    SOCKET sock;
    DWORD err;

    sock = WSASocketW(domain,
                      SOCK_DGRAM,
                      0,
                      NULL,
                      0,
                      WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (sock == INVALID_SOCKET) {
      err = WSAGetLastError();
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }

    err = uv__udp2_set_socket(handle->loop, handle, sock, domain);
    if (err) {
      closesocket(sock);
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }
  }

  return 0;
}


static int uv__udp2_maybe_bind(uv_udp2_t* handle,
                               const struct sockaddr* addr,
                               unsigned int addrlen,
                               unsigned int flags) {
  int r;
  int err;
  DWORD no = 0;

  if (handle->flags & UV_HANDLE_BOUND)
    return 0;

  /* There is no SO_REUSEPORT on Windows. */
  if (flags & UV_UDP2_REUSEPORT)
    return UV_ENOTSUP;

  if ((flags & UV_UDP2_IPV6ONLY) && addr->sa_family != AF_INET6)
    return ERROR_INVALID_PARAMETER;

  if (handle->socket == INVALID_SOCKET) {
    SOCKET sock = WSASocketW(addr->sa_family,
                             SOCK_DGRAM,
                             0,
                             NULL,
                             0,
                             WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (sock == INVALID_SOCKET)
      return WSAGetLastError();

    err = uv__udp2_set_socket(handle->loop, handle, sock, addr->sa_family);
    if (err) {
      closesocket(sock);
      return err;
    }
  }

  if (flags & UV_UDP2_REUSEADDR) {
    DWORD yes = 1;
    if (setsockopt(handle->socket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   (char*) &yes,
                   sizeof yes) == SOCKET_ERROR) {
      return WSAGetLastError();
    }
  }

  if (addr->sa_family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;

  if (addr->sa_family == AF_INET6 && !(flags & UV_UDP2_IPV6ONLY)) {
    /* On Windows IPV6ONLY is on by default. Turn it off for dual-stack. */
    setsockopt(handle->socket,
               IPPROTO_IPV6,
               IPV6_V6ONLY,
               (char*) &no,
               sizeof no);
  }

  r = bind(handle->socket, addr, addrlen);
  if (r == SOCKET_ERROR)
    return WSAGetLastError();

  handle->flags |= UV_HANDLE_BOUND;

  return 0;
}


int uv__udp2_bind(uv_udp2_t* handle,
                  const struct sockaddr* addr,
                  unsigned int addrlen,
                  unsigned int flags) {
  int err;

  err = uv__udp2_maybe_bind(handle, addr, addrlen, flags);
  if (err)
    return uv_translate_sys_error(err);

  /* Apply socket options after bind. */
  if (flags & UV_UDP2_RECVECN) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_RECVTCLASS,
                 (char*) &yes,
                 sizeof yes);
    } else {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_RECVTOS,
                 (char*) &yes,
                 sizeof yes);
    }
    handle->flags |= UV_HANDLE_UDP2_RECVECN;

    /* Obtain WSARecvMsg if not already obtained. */
    if (handle->func_wsarecvmsg == NULL) {
      DWORD bytes;
      LPFN_WSARECVMSG func = NULL;
      if (WSAIoctl(handle->socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   (void*) &uv__wsarecvmsg_guid,
                   sizeof(uv__wsarecvmsg_guid),
                   &func,
                   sizeof(func),
                   &bytes,
                   NULL,
                   NULL) != SOCKET_ERROR) {
        handle->func_wsarecvmsg = func;
      }
    }
  }

  if (flags & UV_UDP2_PMTUD) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD val = IP_PMTUDISC_PROBE;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_MTU_DISCOVER,
                 (char*) &val,
                 sizeof val);
    } else {
      /* On Windows, use IP_DONTFRAGMENT for IPv4 PMTUD. */
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_DONTFRAGMENT,
                 (char*) &yes,
                 sizeof yes);
    }
  }

  if (flags & UV_UDP2_RECVPKTINFO) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_PKTINFO,
                 (char*) &yes,
                 sizeof yes);
    } else {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_PKTINFO,
                 (char*) &yes,
                 sizeof yes);
    }
    handle->flags |= UV_HANDLE_UDP2_RECVPKTINFO;

    /* Obtain WSARecvMsg if not already obtained. */
    if (handle->func_wsarecvmsg == NULL) {
      DWORD bytes;
      LPFN_WSARECVMSG func = NULL;
      if (WSAIoctl(handle->socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   (void*) &uv__wsarecvmsg_guid,
                   sizeof(uv__wsarecvmsg_guid),
                   &func,
                   sizeof(func),
                   &bytes,
                   NULL,
                   NULL) != SOCKET_ERROR) {
        handle->func_wsarecvmsg = func;
      }
    }
  }

  if ((flags & UV_UDP2_GRO) && (flags & UV_UDP2_GRO_RAW))
    return UV_EINVAL;

  if (flags & UV_UDP2_GRO)
    handle->flags |= UV_HANDLE_UDP2_GRO;
  if (flags & UV_UDP2_GRO_RAW)
    handle->flags |= UV_HANDLE_UDP2_GRO_RAW;

  return 0;
}


void uv__udp2_close(uv_loop_t* loop, uv_udp2_t* handle) {
  uv_udp2_recv_stop(handle);
  closesocket(handle->socket);
  handle->socket = INVALID_SOCKET;

  uv__handle_closing(handle);

  if (handle->reqs_pending == 0)
    uv__want_endgame(loop, (uv_handle_t*) handle);
}


void uv__udp2_endgame(uv_loop_t* loop, uv_udp2_t* handle) {
  if (handle->flags & UV_HANDLE_CLOSING &&
      handle->reqs_pending == 0) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    uv__handle_close(handle);
  }
}


static void uv__udp2_queue_recv(uv_loop_t* loop, uv_udp2_t* handle) {
  uv_req_t* req;
  uv_buf_t buf;
  DWORD bytes, flags;
  int result;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  req = &handle->recv_req;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  handle->flags |= UV_HANDLE_ZERO_READ;

  buf.base = (char*) uv_zero_;
  buf.len = 0;
  flags = MSG_PEEK;

  result = handle->func_wsarecv(handle->socket,
                                (WSABUF*) &buf,
                                1,
                                &bytes,
                                &flags,
                                &req->u.io.overlapped,
                                NULL);

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Process the req without IOCP. */
    handle->flags |= UV_HANDLE_READ_PENDING;
    req->u.io.overlapped.InternalHigh = bytes;
    handle->reqs_pending++;
    uv__insert_pending_req(loop, req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* The req will be processed with IOCP. */
    handle->flags |= UV_HANDLE_READ_PENDING;
    handle->reqs_pending++;
  } else {
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, req);
    handle->reqs_pending++;
  }
}


static int uv__udp2_win_parse_ecn(WSAMSG* msg, int is_ipv6) {
  WSACMSGHDR* cm;

  for (cm = WSA_CMSG_FIRSTHDR(msg); cm; cm = WSA_CMSG_NXTHDR(msg, cm)) {
    if (!is_ipv6 &&
        cm->cmsg_level == IPPROTO_IP &&
        cm->cmsg_type == IP_TOS) {
      return *(unsigned char*) WSA_CMSG_DATA(cm) & 0x03;
    }
    if (is_ipv6 &&
        cm->cmsg_level == IPPROTO_IPV6 &&
        cm->cmsg_type == IPV6_TCLASS) {
      DWORD tclass;
      memcpy(&tclass, WSA_CMSG_DATA(cm), sizeof tclass);
      return (int) (tclass & 0x03);
    }
  }
  return 0;
}


static void uv__udp2_win_parse_pktinfo(WSAMSG* msg,
                                        uv_udp2_recv_t* recv_info) {
  WSACMSGHDR* cm;

  memset(&recv_info->local, 0, sizeof recv_info->local);
  recv_info->ifindex = 0;

  for (cm = WSA_CMSG_FIRSTHDR(msg); cm; cm = WSA_CMSG_NXTHDR(msg, cm)) {
    if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO) {
      IN_PKTINFO pki;
      struct sockaddr_in* a;
      memcpy(&pki, WSA_CMSG_DATA(cm), sizeof pki);
      a = (struct sockaddr_in*) &recv_info->local;
      a->sin_family = AF_INET;
      a->sin_addr = pki.ipi_addr;
      recv_info->ifindex = pki.ipi_ifindex;
    }
    if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
      IN6_PKTINFO pki;
      struct sockaddr_in6* a;
      memcpy(&pki, WSA_CMSG_DATA(cm), sizeof pki);
      a = (struct sockaddr_in6*) &recv_info->local;
      a->sin6_family = AF_INET6;
      a->sin6_addr = pki.ipi6_addr;
      recv_info->ifindex = pki.ipi6_ifindex;
    }
  }
}


void uv__process_udp2_recv_req(uv_loop_t* loop, uv_udp2_t* handle,
    uv_req_t* req) {
  uv_udp2_recv_t recv_info;
  uv_buf_t buf;

  assert(handle->type == UV_UDP2);

  handle->flags &= ~UV_HANDLE_READ_PENDING;

  if (!REQ_SUCCESS(req)) {
    DWORD err = GET_REQ_SOCK_ERROR(req);
    if (err == WSAEMSGSIZE) {
      /* Not a real error, it just indicates that the received packet was
       * bigger than the receive buffer. */
    } else if (err == WSAECONNRESET || err == WSAENETRESET) {
      /* A previous sendto operation failed; ignore this error. If zero-reading
       * we need to call WSARecv/WSARecvFrom _without_ the MSG_PEEK flag to
       * clear out the error queue. For nonzero reads, immediately queue a new
       * receive. */
      if (!(handle->flags & UV_HANDLE_ZERO_READ))
        goto done;
    } else {
      /* A real error occurred. Report the error to the user only if we're
       * currently reading. */
      if (handle->flags & UV_HANDLE_READING) {
        uv_udp2_recv_stop(handle);
        memset(&recv_info, 0, sizeof(recv_info));
        buf = (handle->flags & UV_HANDLE_ZERO_READ) ?
              uv_buf_init(NULL, 0) : handle->recv_buffer;
        recv_info.nread = uv_translate_sys_error(err);
        recv_info.buf = &buf;
        recv_info.addr = NULL;
        handle->recv_cb(handle, &recv_info);
      }
      goto done;
    }
  }

  if (!(handle->flags & UV_HANDLE_ZERO_READ)) {
    /* Nonzero IOCP read completion. queue_recv always uses zero-read,
     * so this path is not reached in normal operation. */
    int partial = !REQ_SUCCESS(req);
    memset(&recv_info, 0, sizeof(recv_info));
    recv_info.nread = req->u.io.overlapped.InternalHigh;
    recv_info.buf = &handle->recv_buffer;
    recv_info.addr = (const struct sockaddr*) &handle->recv_from;
    recv_info.flags = partial ? UV_UDP2_PARTIAL : 0;
    handle->recv_cb(handle, &recv_info);
  } else if (handle->flags & UV_HANDLE_READING) {
    DWORD bytes, err, flags;
    struct sockaddr_storage from;
    int from_len;
    int count;

    /* Prevent loop starvation when the data comes in as fast as
     * (or faster than) we can read it. */
    count = 32;

    do {
      buf = uv_buf_init(NULL, 0);
      handle->alloc_cb(handle, UV__UDP_DGRAM_MAXSIZE, &buf);
      if (buf.base == NULL || buf.len == 0) {
        memset(&recv_info, 0, sizeof(recv_info));
        recv_info.nread = UV_ENOBUFS;
        recv_info.buf = &buf;
        handle->recv_cb(handle, &recv_info);
        goto done;
      }

      memset(&from, 0, sizeof from);
      from_len = sizeof from;
      flags = 0;

      if (handle->func_wsarecvmsg != NULL) {
        WSAMSG msg;
        WSABUF data;
        char control[256];
        int is_ipv6;

        data.buf = buf.base;
        data.len = (ULONG) buf.len;

        memset(&msg, 0, sizeof msg);
        msg.name = (LPSOCKADDR) &from;
        msg.namelen = sizeof from;
        msg.lpBuffers = &data;
        msg.dwBufferCount = 1;
        msg.Control.buf = control;
        msg.Control.len = sizeof control;

        is_ipv6 = !!(handle->flags & UV_HANDLE_IPV6);

        if (handle->func_wsarecvmsg(handle->socket,
                                     &msg,
                                     &bytes,
                                     NULL,
                                     NULL) != SOCKET_ERROR) {
          err = ERROR_SUCCESS;
          memset(&recv_info, 0, sizeof(recv_info));
          recv_info.nread = bytes;
          recv_info.buf = &buf;
          recv_info.addr = (const struct sockaddr*) &from;
          if (handle->flags & UV_HANDLE_UDP2_RECVECN)
            recv_info.ecn = uv__udp2_win_parse_ecn(&msg, is_ipv6);
          if (handle->flags & UV_HANDLE_UDP2_RECVPKTINFO)
            uv__udp2_win_parse_pktinfo(&msg, &recv_info);
          handle->recv_cb(handle, &recv_info);
        } else {
          err = WSAGetLastError();
          if (err == WSAEMSGSIZE) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = bytes;
            recv_info.buf = &buf;
            recv_info.addr = (const struct sockaddr*) &from;
            recv_info.flags = UV_UDP2_PARTIAL;
            if (handle->flags & UV_HANDLE_UDP2_RECVECN)
              recv_info.ecn = uv__udp2_win_parse_ecn(&msg, is_ipv6);
            if (handle->flags & UV_HANDLE_UDP2_RECVPKTINFO)
              uv__udp2_win_parse_pktinfo(&msg, &recv_info);
            handle->recv_cb(handle, &recv_info);
            err = ERROR_SUCCESS;
          } else if (err == WSAEWOULDBLOCK) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = 0;
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          } else if (err == WSAECONNRESET || err == WSAENETRESET) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = 0;
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          } else {
            uv_udp2_recv_stop(handle);
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = uv_translate_sys_error(err);
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          }
        }
      } else {
        /* No WSARecvMsg available; fall back to WSARecvFrom. */
        if (WSARecvFrom(handle->socket,
                        (WSABUF*) &buf,
                        1,
                        &bytes,
                        &flags,
                        (struct sockaddr*) &from,
                        &from_len,
                        NULL,
                        NULL) != SOCKET_ERROR) {
          err = ERROR_SUCCESS;
          memset(&recv_info, 0, sizeof(recv_info));
          recv_info.nread = bytes;
          recv_info.buf = &buf;
          recv_info.addr = (const struct sockaddr*) &from;
          handle->recv_cb(handle, &recv_info);
        } else {
          err = WSAGetLastError();
          if (err == WSAEMSGSIZE) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = bytes;
            recv_info.buf = &buf;
            recv_info.addr = (const struct sockaddr*) &from;
            recv_info.flags = UV_UDP2_PARTIAL;
            handle->recv_cb(handle, &recv_info);
            err = ERROR_SUCCESS;
          } else if (err == WSAEWOULDBLOCK) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = 0;
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          } else if (err == WSAECONNRESET || err == WSAENETRESET) {
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = 0;
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          } else {
            uv_udp2_recv_stop(handle);
            memset(&recv_info, 0, sizeof(recv_info));
            recv_info.nread = uv_translate_sys_error(err);
            recv_info.buf = &buf;
            handle->recv_cb(handle, &recv_info);
          }
        }
      }
    } while (err == ERROR_SUCCESS &&
             count-- > 0 &&
             (handle->flags & UV_HANDLE_READING) &&
             !(handle->flags & UV_HANDLE_READ_PENDING));
  }

done:
  /* Post another read if still reading and not closing. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__udp2_queue_recv(loop, handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_udp2_send_req(uv_loop_t* loop, uv_udp2_t* handle,
    uv_udp2_send_t* req) {
  int err;

  assert(handle->type == UV_UDP2);

  assert(handle->send_queue_size >= req->u.io.queued_bytes);
  assert(handle->send_queue_count >= 1);
  handle->send_queue_size -= req->u.io.queued_bytes;
  handle->send_queue_count--;

  UNREGISTER_HANDLE_REQ(loop, handle);

  if (req->cb) {
    err = 0;
    if (!REQ_SUCCESS(req))
      err = GET_REQ_SOCK_ERROR(req);
    req->cb(req, uv_translate_sys_error(err));
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


int uv__udp2_connect(uv_udp2_t* handle,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  const struct sockaddr* bind_addr;
  int err;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;

    err = uv__udp2_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = connect(handle->socket, addr, addrlen);
  if (err)
    return uv_translate_sys_error(WSAGetLastError());

  handle->flags |= UV_HANDLE_UDP2_CONNECTED;

  return 0;
}


int uv__udp2_disconnect(uv_udp2_t* handle) {
  int err;
  struct sockaddr_storage addr;

  memset(&addr, 0, sizeof(addr));

  err = connect(handle->socket, (struct sockaddr*) &addr, sizeof(addr));
  if (err)
    return uv_translate_sys_error(WSAGetLastError());

  handle->flags &= ~UV_HANDLE_UDP2_CONNECTED;
  return 0;
}


int uv__udp2_is_connected(uv_udp2_t* handle) {
  struct sockaddr_storage addr;
  int addrlen;

  addrlen = sizeof(addr);
  if (getpeername(handle->socket, (struct sockaddr*) &addr, &addrlen) != 0)
    return 0;

  return addr.ss_family != AF_UNSPEC;
}


static int uv__udp2_send_impl(uv_udp2_send_t* req,
                              uv_udp2_t* handle,
                              const uv_buf_t bufs[],
                              unsigned int nbufs,
                              const struct sockaddr* addr,
                              unsigned int addrlen,
                              uv_udp2_send_cb cb) {
  uv_loop_t* loop = handle->loop;
  DWORD result, bytes;

  UV_REQ_INIT(req, UV_UDP_SEND);
  req->handle = handle;
  req->cb = cb;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  result = WSASendTo(handle->socket,
                     (WSABUF*) bufs,
                     nbufs,
                     &bytes,
                     0,
                     addr,
                     addrlen,
                     &req->u.io.overlapped,
                     NULL);

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Request completed immediately. */
    req->u.io.queued_bytes = 0;
    handle->reqs_pending++;
    handle->send_queue_size += req->u.io.queued_bytes;
    handle->send_queue_count++;
    REGISTER_HANDLE_REQ(loop, handle);
    uv__insert_pending_req(loop, (uv_req_t*) req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* Request queued by the kernel. */
    req->u.io.queued_bytes = uv__count_bufs(bufs, nbufs);
    handle->reqs_pending++;
    handle->send_queue_size += req->u.io.queued_bytes;
    handle->send_queue_count++;
    REGISTER_HANDLE_REQ(loop, handle);
  } else {
    /* Send failed due to an error. */
    return WSAGetLastError();
  }

  return 0;
}


int uv__udp2_send(uv_udp2_send_t* req,
                  uv_udp2_t* handle,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  const struct sockaddr* addr,
                  unsigned int addrlen,
                  uv_udp2_send_cb send_cb) {
  const struct sockaddr* bind_addr;
  int err;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;

    err = uv__udp2_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = uv__udp2_send_impl(req, handle, bufs, nbufs, addr, addrlen, send_cb);
  if (err)
    return uv_translate_sys_error(err);

  return 0;
}


int uv__udp2_try_send(uv_udp2_t* handle,
                      const uv_buf_t bufs[],
                      unsigned int nbufs,
                      const struct sockaddr* addr,
                      unsigned int addrlen) {
  DWORD bytes;
  const struct sockaddr* bind_addr;
  struct sockaddr_storage converted;
  int err;

  if (addr != NULL) {
    err = uv__convert_to_localhost_if_unspecified(addr, &converted);
    if (err)
      return err;
    addr = (const struct sockaddr*) &converted;
  }

  /* Already sending a message. */
  if (handle->send_queue_count != 0)
    return UV_EAGAIN;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    else if (addrlen == sizeof(uv_addr_ip6_any_))
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    else
      return UV_EINVAL;
    err = uv__udp2_maybe_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return uv_translate_sys_error(err);
  }

  err = WSASendTo(handle->socket,
                  (WSABUF*) bufs,
                  nbufs,
                  &bytes,
                  0,
                  addr,
                  addrlen,
                  NULL,
                  NULL);

  if (err)
    return uv_translate_sys_error(WSAGetLastError());

  return bytes;
}


int uv__udp2_try_send2(uv_udp2_t* handle,
                       unsigned int count,
                       uv_buf_t* bufs[/*count*/],
                       unsigned int nbufs[/*count*/],
                       struct sockaddr* addrs[/*count*/]) {
  int i;
  int r;

  if (count > INT_MAX)
    return UV_EINVAL;

  for (i = 0; i < (int) count; i++) {
    r = uv_udp2_try_send(handle, bufs[i], nbufs[i], addrs[i]);
    if (r < 0)
      return i > 0 ? i : (int) r;
  }

  return i;
}


int uv__udp2_recv_start(uv_udp2_t* handle, uv_udp2_alloc_cb alloc_cb,
    uv_udp2_recv_cb recv_cb) {
  uv_loop_t* loop = handle->loop;
  int err;

  if (handle->flags & UV_HANDLE_READING)
    return UV_EALREADY;

  err = uv__udp2_maybe_bind(handle,
                             (const struct sockaddr*) &uv_addr_ip4_any_,
                             sizeof(uv_addr_ip4_any_),
                             0);
  if (err)
    return uv_translate_sys_error(err);

  handle->flags |= UV_HANDLE_READING;
  INCREASE_ACTIVE_COUNT(loop, handle);

  handle->recv_cb = recv_cb;
  handle->alloc_cb = alloc_cb;

  /* If reading was stopped and then started again, there could still be a recv
   * request pending. */
  if (!(handle->flags & UV_HANDLE_READ_PENDING))
    uv__udp2_queue_recv(loop, handle);

  return 0;
}


int uv__udp2_recv_stop(uv_udp2_t* handle) {
  if (handle->flags & UV_HANDLE_READING) {
    handle->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, handle);
  }

  return 0;
}


/*
 * Public API functions (Windows-only implementations)
 */

int uv_udp2_open(uv_udp2_t* handle, uv_os_sock_t sock) {
  WSAPROTOCOL_INFOW protocol_info;
  int opt_len;
  int err;

  /* Detect the address family of the socket. */
  opt_len = (int) sizeof protocol_info;
  if (getsockopt(sock,
                 SOL_SOCKET,
                 SO_PROTOCOL_INFOW,
                 (char*) &protocol_info,
                 &opt_len) == SOCKET_ERROR) {
    return uv_translate_sys_error(GetLastError());
  }

  err = uv__udp2_set_socket(handle->loop,
                             handle,
                             sock,
                             protocol_info.iAddressFamily);
  if (err)
    return uv_translate_sys_error(err);

  if (uv__udp2_is_connected(handle))
    handle->flags |= UV_HANDLE_UDP2_CONNECTED;

  return 0;
}


int uv_udp2_getsockname(const uv_udp2_t* handle,
                        struct sockaddr* name,
                        int* namelen) {
  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen,
                             0);
}


int uv_udp2_getpeername(const uv_udp2_t* handle,
                        struct sockaddr* name,
                        int* namelen) {
  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen,
                             0);
}


int uv_udp2_set_ecn(uv_udp2_t* handle, int ecn) {
  int r;

  if (ecn < 0 || ecn > 3)
    return UV_EINVAL;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (handle->flags & UV_HANDLE_IPV6) {
    DWORD val = (DWORD) ecn;
    r = setsockopt(handle->socket,
                   IPPROTO_IPV6,
                   IPV6_TCLASS,
                   (char*) &val,
                   sizeof val);
  } else {
    /* Preserve DSCP bits, set only the low 2 (ECN) bits. */
    DWORD tos = 0;
    int len = sizeof(tos);
    getsockopt(handle->socket, IPPROTO_IP, IP_TOS, (char*) &tos, &len);
    tos = (tos & ~0x03) | ((DWORD) ecn & 0x03);
    r = setsockopt(handle->socket,
                   IPPROTO_IP,
                   IP_TOS,
                   (char*) &tos,
                   sizeof tos);
  }

  if (r == SOCKET_ERROR)
    return uv_translate_sys_error(WSAGetLastError());

  return 0;
}


int uv_udp2_set_pmtud(uv_udp2_t* handle, enum uv_udp2_pmtud mode) {
  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (handle->flags & UV_HANDLE_IPV6) {
    DWORD val;
    switch (mode) {
      case UV_UDP2_PMTUD_OFF:
        val = IP_PMTUDISC_NOT_SET;
        break;
      case UV_UDP2_PMTUD_DO:
        val = IP_PMTUDISC_DO;
        break;
      case UV_UDP2_PMTUD_PROBE:
        val = IP_PMTUDISC_PROBE;
        break;
      default:
        return UV_EINVAL;
    }
    if (setsockopt(handle->socket,
                   IPPROTO_IPV6,
                   IPV6_MTU_DISCOVER,
                   (char*) &val,
                   sizeof val) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  } else {
    /* IPv4: use IP_DONTFRAGMENT as a boolean, and IP_MTU_DISCOVER for mode. */
    DWORD val;
    switch (mode) {
      case UV_UDP2_PMTUD_OFF:
        val = IP_PMTUDISC_NOT_SET;
        break;
      case UV_UDP2_PMTUD_DO:
        val = IP_PMTUDISC_DO;
        break;
      case UV_UDP2_PMTUD_PROBE:
        val = IP_PMTUDISC_PROBE;
        break;
      default:
        return UV_EINVAL;
    }
    if (setsockopt(handle->socket,
                   IPPROTO_IP,
                   IP_MTU_DISCOVER,
                   (char*) &val,
                   sizeof val) == SOCKET_ERROR) {
      /* Fallback: try IP_DONTFRAGMENT if IP_MTU_DISCOVER is not available. */
      if (mode != UV_UDP2_PMTUD_OFF) {
        DWORD yes = 1;
        if (setsockopt(handle->socket,
                       IPPROTO_IP,
                       IP_DONTFRAGMENT,
                       (char*) &yes,
                       sizeof yes) == SOCKET_ERROR) {
          return uv_translate_sys_error(WSAGetLastError());
        }
      } else {
        DWORD no = 0;
        if (setsockopt(handle->socket,
                       IPPROTO_IP,
                       IP_DONTFRAGMENT,
                       (char*) &no,
                       sizeof no) == SOCKET_ERROR) {
          return uv_translate_sys_error(WSAGetLastError());
        }
      }
    }
  }

  return 0;
}


int uv_udp2_getmtu(const uv_udp2_t* handle, size_t* mtu) {
  DWORD val;
  int len;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (mtu == NULL)
    return UV_EINVAL;

  len = sizeof(val);

  if (handle->flags & UV_HANDLE_IPV6) {
    if (getsockopt(handle->socket,
                   IPPROTO_IPV6,
                   IPV6_MTU,
                   (char*) &val,
                   &len) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  } else {
    if (getsockopt(handle->socket,
                   IPPROTO_IP,
                   IP_MTU,
                   (char*) &val,
                   &len) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  }

  *mtu = (size_t) val;
  return 0;
}


int uv_udp2_configure(uv_udp2_t* handle, unsigned int flags) {
  /* Only accept the configuration flags. */
  if (flags & ~(UV_UDP2_RECVECN | UV_UDP2_PMTUD | UV_UDP2_RECVPKTINFO))
    return UV_EINVAL;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (flags & UV_UDP2_RECVECN) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_RECVTCLASS,
                 (char*) &yes,
                 sizeof yes);
    } else {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_RECVTOS,
                 (char*) &yes,
                 sizeof yes);
    }
    handle->flags |= UV_HANDLE_UDP2_RECVECN;

    /* Obtain WSARecvMsg if not already obtained. */
    if (handle->func_wsarecvmsg == NULL) {
      DWORD bytes;
      LPFN_WSARECVMSG func = NULL;
      if (WSAIoctl(handle->socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   (void*) &uv__wsarecvmsg_guid,
                   sizeof(uv__wsarecvmsg_guid),
                   &func,
                   sizeof(func),
                   &bytes,
                   NULL,
                   NULL) != SOCKET_ERROR) {
        handle->func_wsarecvmsg = func;
      }
    }
  }

  if (flags & UV_UDP2_PMTUD) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD val = IP_PMTUDISC_PROBE;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_MTU_DISCOVER,
                 (char*) &val,
                 sizeof val);
    } else {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_DONTFRAGMENT,
                 (char*) &yes,
                 sizeof yes);
    }
  }

  if (flags & UV_UDP2_RECVPKTINFO) {
    if (handle->flags & UV_HANDLE_IPV6) {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IPV6,
                 IPV6_PKTINFO,
                 (char*) &yes,
                 sizeof yes);
    } else {
      DWORD yes = 1;
      setsockopt(handle->socket,
                 IPPROTO_IP,
                 IP_PKTINFO,
                 (char*) &yes,
                 sizeof yes);
    }
    handle->flags |= UV_HANDLE_UDP2_RECVPKTINFO;

    /* Obtain WSARecvMsg if not already obtained. */
    if (handle->func_wsarecvmsg == NULL) {
      DWORD bytes;
      LPFN_WSARECVMSG func = NULL;
      if (WSAIoctl(handle->socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   (void*) &uv__wsarecvmsg_guid,
                   sizeof(uv__wsarecvmsg_guid),
                   &func,
                   sizeof(func),
                   &bytes,
                   NULL,
                   NULL) != SOCKET_ERROR) {
        handle->func_wsarecvmsg = func;
      }
    }
  }

  return 0;
}


int uv_udp2_using_recvmmsg(const uv_udp2_t* handle) {
  /* recvmmsg is not available on Windows. */
  return 0;
}


int uv_udp2_set_ttl(uv_udp2_t* handle, int ttl) {
  DWORD optval = (DWORD) ttl;

  if (ttl < 1 || ttl > 255)
    return UV_EINVAL;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (handle->flags & UV_HANDLE_IPV6) {
    if (setsockopt(handle->socket,
                   IPPROTO_IPV6,
                   IPV6_HOPLIMIT,
                   (char*) &optval,
                   sizeof optval) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  } else {
    if (setsockopt(handle->socket,
                   IPPROTO_IP,
                   IP_TTL,
                   (char*) &optval,
                   sizeof optval) == SOCKET_ERROR) {
      return uv_translate_sys_error(WSAGetLastError());
    }
  }

  return 0;
}


int uv_udp2_set_broadcast(uv_udp2_t* handle, int on) {
  BOOL optval = (BOOL) on;

  if (handle->socket == INVALID_SOCKET)
    return UV_EBADF;

  if (setsockopt(handle->socket,
                 SOL_SOCKET,
                 SO_BROADCAST,
                 (char*) &optval,
                 sizeof optval) == SOCKET_ERROR) {
    return uv_translate_sys_error(WSAGetLastError());
  }

  return 0;
}


int uv_udp2_try_send_batch(uv_udp2_t* handle,
                            uv_udp2_mmsg_t* msgs,
                            unsigned int count) {
  unsigned int i;
  int r;
  int nsent;

  if (handle->socket == INVALID_SOCKET)
    return UV_EINVAL;

  if (count == 0)
    return 0;

  /* No GSO on Windows; send each message individually. */
  nsent = 0;
  for (i = 0; i < count; i++) {
    const struct sockaddr* sa;
    int addrlen;
    DWORD bytes;

    sa = msgs[i].addr;
    addrlen = 0;
    if (sa != NULL)
      addrlen = sa->sa_family == AF_INET6 ?
          sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

    r = WSASendTo(handle->socket,
                  (WSABUF*) msgs[i].bufs,
                  msgs[i].nbufs,
                  &bytes,
                  0,
                  sa,
                  addrlen,
                  NULL,
                  NULL);
    if (r == SOCKET_ERROR) {
      if (nsent > 0)
        return nsent;
      return uv_translate_sys_error(WSAGetLastError());
    }
    nsent++;
  }
  return nsent;
}


unsigned int uv_udp2_gso_max_segments(const uv_udp2_t* handle) {
  (void) handle;
  return 0;
}


int uv_udp2_set_cpu_affinity(uv_udp2_t* handle, int cpu) {
  (void) handle;
  (void) cpu;
  return UV_ENOTSUP;
}
