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
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>

#include "oio.h"
#include "tree.h"

/*
 * Guids and typedefs for winsock extension functions
 * Mingw32 doesn't have these :-(
 */
#ifndef WSAID_ACCEPTEX
# define WSAID_ACCEPTEX                                        \
         {0xb5367df1, 0xcbac, 0x11cf,                          \
         {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}

# define WSAID_CONNECTEX                                       \
         {0x25a207b9, 0xddf3, 0x4660,                          \
         {0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e}}

# define WSAID_GETACCEPTEXSOCKADDRS                            \
         {0xb5367df2, 0xcbac, 0x11cf,                          \
         {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}

# define WSAID_DISCONNECTEX                                    \
         {0x7fda2e11, 0x8630, 0x436f,                          \
         {0xa0, 0x31, 0xf5, 0x36, 0xa6, 0xee, 0xc1, 0x57}}

# define WSAID_TRANSMITFILE                                    \
         {0xb5367df0, 0xcbac, 0x11cf,                          \
         {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}

  typedef BOOL(*LPFN_ACCEPTEX)
              (SOCKET sListenSocket,
               SOCKET sAcceptSocket,
               PVOID lpOutputBuffer,
               DWORD dwReceiveDataLength,
               DWORD dwLocalAddressLength,
               DWORD dwRemoteAddressLength,
               LPDWORD lpdwBytesReceived,
               LPOVERLAPPED lpOverlapped);

  typedef BOOL(*LPFN_CONNECTEX)
              (SOCKET s,
               const struct sockaddr *name,
               int namelen,
               PVOID lpSendBuffer,
               DWORD dwSendDataLength,
               LPDWORD lpdwBytesSent,
               LPOVERLAPPED lpOverlapped);

  typedef void(*LPFN_GETACCEPTEXSOCKADDRS)
              (PVOID lpOutputBuffer,
               DWORD dwReceiveDataLength,
               DWORD dwLocalAddressLength,
               DWORD dwRemoteAddressLength,
               LPSOCKADDR *LocalSockaddr,
               LPINT LocalSockaddrLength,
               LPSOCKADDR *RemoteSockaddr,
               LPINT RemoteSockaddrLength);

  typedef BOOL(*LPFN_DISCONNECTEX)
              (SOCKET hSocket,
               LPOVERLAPPED lpOverlapped,
               DWORD dwFlags,
               DWORD reserved);

  typedef BOOL(*LPFN_TRANSMITFILE)
              (SOCKET hSocket,
               HANDLE hFile,
               DWORD nNumberOfBytesToWrite,
               DWORD nNumberOfBytesPerSend,
               LPOVERLAPPED lpOverlapped,
               LPTRANSMIT_FILE_BUFFERS lpTransmitBuffers,
               DWORD dwFlags);
#endif

/*
 * MinGW is missing this too
 */
#ifndef SO_UPDATE_CONNECT_CONTEXT
# define SO_UPDATE_CONNECT_CONTEXT   0x7010
#endif

/*
 * Described in MSDN but apparently not defined in the SDK.
 */
#ifndef ERROR_SUCCESS
# define ERROR_SUCCESS  0
#endif


/*
 * Pointers to winsock extension functions to be retrieved dynamically
 */
static LPFN_CONNECTEX            pConnectEx;
static LPFN_ACCEPTEX             pAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS pGetAcceptExSockAddrs;
static LPFN_DISCONNECTEX         pDisconnectEx;
static LPFN_TRANSMITFILE         pTransmitFile;


/*
 * Private oio_handle flags
 */
#define OIO_HANDLE_CLOSING   0x01
#define OIO_HANDLE_CLOSED    0x02
#define OIO_HANDLE_BOUND     0x04

/*
 * Private oio_req flags.
 */
/* The request is currently queued. */
#define OIO_REQ_PENDING      0x01


/*
 * Special oio_req type used by AcceptEx calls
 */
typedef struct oio_accept_req_s {
  struct oio_req_s req;
  SOCKET socket;

  /* AcceptEx specifies that the buffer must be big enough to at least hold */
  /* two socket addresses plus 32 bytes. */
  char buffer[sizeof(struct sockaddr_storage) * 2 + 32];
} oio_accept_req;


/* Binary tree used to keep the list of timers sorted. */
static int oio_timer_compare(oio_req* t1, oio_req* t2);
RB_HEAD(oio_timer_s, oio_req_s);
RB_PROTOTYPE_STATIC(oio_timer_s, oio_req_s, tree_entry, oio_timer_compare);

/* The head of the timers tree */
static struct oio_timer_s oio_timers_ = RB_INITIALIZER(oio_timers_);


/* Head of a single-linked list of closed handles */
static oio_handle* oio_closed_handles_ = NULL;


/* The current time according to the event loop. in msecs. */
static int64_t oio_now_ = 0;
static int64_t oio_ticks_per_msec_ = 0;


/*
 * Global I/O completion port
 */
static HANDLE oio_iocp_;


/* Global error code */
static const oio_err oio_ok_ = { OIO_OK, ERROR_SUCCESS };
static oio_err oio_last_error_ = { OIO_OK, ERROR_SUCCESS };


/* Reference count that keeps the event loop alive */
static int oio_refs_ = 0;


/* Ip address used to bind to any port at any interface */
static struct sockaddr_in oio_addr_ip4_any_;


/*
 * Display an error message and abort the event loop.
 */
static void oio_fatal_error(const int errorno, const char *syscall) {
  char *buf = NULL;
  const char *errmsg;

  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorno,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);

  if (buf) {
    errmsg = buf;
  } else {
    errmsg = "Unknown error";
  }

  /* FormatMessage messages include a newline character already, */
  /* so don't add another. */
  if (syscall) {
    fprintf(stderr, "%s: (%d) %s", syscall, errorno, errmsg);
  } else {
    fprintf(stderr, "(%d) %s", errorno, errmsg);
  }

  if (buf) {
    LocalFree(buf);
  }

  *((char*)NULL) = 0xff; /* Force debug break */
  abort();
}


oio_err oio_last_error() {
  return oio_last_error_;
}


static oio_err_code oio_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case ERROR_SUCCESS:                 return OIO_OK;
    case ERROR_TOO_MANY_OPEN_FILES:     return OIO_EMFILE;
    case WSAEMFILE:                     return OIO_EMFILE;
    case WSAEINVAL:                     return OIO_EINVAL;
    case WSAEALREADY:                   return OIO_EALREADY;
    case ERROR_OUTOFMEMORY:             return OIO_ENOMEM;
    case ERROR_CONNECTION_REFUSED:      return OIO_ECONNREFUSED;
    default:                            return OIO_UNKNOWN;
  }
}


static oio_err oio_new_sys_error(int sys_errno) {
  oio_err e;
  e.code = oio_translate_sys_error(sys_errno);
  e.sys_errno_ = sys_errno;
  return e;
}


static void oio_set_sys_error(int sys_errno) {
  oio_last_error_.code = oio_translate_sys_error(sys_errno);
  oio_last_error_.sys_errno_ = sys_errno;
}


/*
 * Retrieves the pointer to a winsock extension function.
 */
static void oio_get_extension_function(SOCKET socket, GUID guid,
    void **target) {
  DWORD result, bytes;

  result = WSAIoctl(socket,
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guid,
                    sizeof(guid),
                    (void*)target,
                    sizeof(*target),
                    &bytes,
                    NULL,
                    NULL);

  if (result == SOCKET_ERROR) {
    *target = NULL;
    oio_fatal_error(WSAGetLastError(),
                    "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
  }
}


void oio_init() {
  const GUID wsaid_connectex            = WSAID_CONNECTEX;
  const GUID wsaid_acceptex             = WSAID_ACCEPTEX;
  const GUID wsaid_getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
  const GUID wsaid_disconnectex         = WSAID_DISCONNECTEX;
  const GUID wsaid_transmitfile         = WSAID_TRANSMITFILE;

  WSADATA wsa_data;
  int errorno;
  LARGE_INTEGER timer_frequency;
  SOCKET dummy;

  /* Initialize winsock */
  errorno = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (errorno != 0) {
    oio_fatal_error(errorno, "WSAStartup");
  }

  /* Set implicit binding address used by connectEx */
  oio_addr_ip4_any_ = oio_ip4_addr("0.0.0.0", 0);

  /* Retrieve the needed winsock extension function pointers. */
  dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (dummy == INVALID_SOCKET) {
    oio_fatal_error(WSAGetLastError(), "socket");
  }

  oio_get_extension_function(dummy,
                             wsaid_connectex,
                             (void**)&pConnectEx);
  oio_get_extension_function(dummy,
                             wsaid_acceptex,
                             (void**)&pAcceptEx);
  oio_get_extension_function(dummy,
                             wsaid_getacceptexsockaddrs,
                             (void**)&pGetAcceptExSockAddrs);
  oio_get_extension_function(dummy,
                             wsaid_disconnectex,
                             (void**)&pDisconnectEx);
  oio_get_extension_function(dummy,
                             wsaid_transmitfile,
                             (void**)&pTransmitFile);

  if (closesocket(dummy) == SOCKET_ERROR) {
    oio_fatal_error(WSAGetLastError(), "closesocket");
  }

  /* Create an I/O completion port */
  oio_iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (oio_iocp_ == NULL) {
    oio_fatal_error(GetLastError(), "CreateIoCompletionPort");
  }

  /* Initialize the event loop time */
  if (!QueryPerformanceFrequency(&timer_frequency))
    oio_fatal_error(GetLastError(), "QueryPerformanceFrequency");
  oio_ticks_per_msec_ = timer_frequency.QuadPart / 1000;

  oio_update_time();
}


void oio_req_init(oio_req* req, oio_handle* handle, void *cb) {
  req->type = OIO_UNKNOWN_REQ;
  req->flags = 0;
  req->handle = handle;
  req->cb = cb;
}


static oio_req* oio_overlapped_to_req(OVERLAPPED* overlapped) {
  return CONTAINING_RECORD(overlapped, oio_req, overlapped);
}


static int oio_set_socket_options(SOCKET socket) {
  DWORD yes = 1;

  /* Set the SO_REUSEADDR option on the socket */
  /* If it fails, soit. */
  setsockopt(socket,
             SOL_SOCKET,
             SO_REUSEADDR,
             (char*)&yes,
             sizeof(int));

  /* Make the socket non-inheritable */
  if (!SetHandleInformation((HANDLE)socket, HANDLE_FLAG_INHERIT, 0)) {
    oio_set_sys_error(GetLastError());
    return -1;
  }

  /* Associate it with the I/O completion port. */
  /* Use oio_handle pointer as completion key. */
  if (CreateIoCompletionPort((HANDLE)socket,
                             oio_iocp_,
                             (ULONG_PTR)socket,
                             0) == NULL) {
    oio_set_sys_error(GetLastError());
    return -1;
  }

  return 0;
}


int oio_tcp_init(oio_handle *handle, oio_close_cb close_cb,
    void* data) {
  handle->close_cb = close_cb;
  handle->data = data;
  handle->type = OIO_TCP;
  handle->flags = 0;
  handle->reqs_pending = 0;
  handle->error = oio_ok_;
  handle->accept_reqs = NULL;
  handle->accepted_socket = INVALID_SOCKET;

  handle->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (handle->socket == INVALID_SOCKET) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  if (oio_set_socket_options(handle->socket) != 0) {
    closesocket(handle->socket);
    return -1;
  }

  oio_refs_++;

  return 0;
}


int oio_accept(oio_handle* server, oio_handle* client,
    oio_close_cb close_cb, void* data) {
  if (!server->accepted_socket == INVALID_SOCKET) {
    oio_set_sys_error(WSAENOTCONN);
    return -1;
  }

  client->close_cb = close_cb;
  client->data = data;
  client->type = OIO_TCP;
  client->socket = server->accepted_socket;
  client->flags = 0;
  client->reqs_pending = 0;
  client->error = oio_ok_;
  client->accepted_socket = INVALID_SOCKET;
  client->accept_reqs = NULL;

  server->accepted_socket = INVALID_SOCKET;
  oio_refs_++;

  return 0;
}


static void oio_close_ready(oio_handle* handle) {
  assert(handle->flags & OIO_HANDLE_CLOSING);
  assert(!(handle->flags & OIO_HANDLE_CLOSED));
  assert(handle->reqs_pending == 0);

  handle->closed_next = oio_closed_handles_;
  oio_closed_handles_ = handle;
}


static int oio_close_error(oio_handle* handle, oio_err e) {
  if (handle->flags & OIO_HANDLE_CLOSING) {
    return 0;
  }

  handle->error = e;

  switch (handle->type) {
    case OIO_TCP:
      closesocket(handle->socket);
      handle->flags |= OIO_HANDLE_CLOSING;

      /* If there are no pending requests for this handle, enqueue the close */
      /* callback immediately. Otherwise oio_poll will do it after the last */
      /* request returns. */
      if (handle->reqs_pending == 0) {
        oio_close_ready(handle);
      }
      return 0;

    default:
      /* Not supported */
      assert(0);
      return -1;
  }
}


int oio_close(oio_handle* handle) {
  return oio_close_error(handle, oio_ok_);
}


static void oio_call_close_cbs() {
  oio_handle *handle;

  while (oio_closed_handles_) {
    handle = oio_closed_handles_;
    oio_closed_handles_ = handle->closed_next;

    assert(handle->flags & OIO_HANDLE_CLOSING);
    assert(!(handle->flags & OIO_HANDLE_CLOSED));
    assert(handle->reqs_pending == 0);

    handle->flags |= OIO_HANDLE_CLOSED;
    oio_refs_--;

    if (handle->accept_reqs) {
      free(handle->accept_reqs);
    }
    if (handle->close_cb) {
        oio_last_error_ = handle->error;
        handle->close_cb(handle, handle->error.code == OIO_OK ? 0 : 1);
    }
  }
}


struct sockaddr_in oio_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int oio_bind(oio_handle* handle, struct sockaddr* addr) {
  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
  } else {
    assert(0);
    return -1;
  }

  if (bind(handle->socket, addr, addrsize) == SOCKET_ERROR) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  handle->flags |= OIO_HANDLE_BOUND;

  return 0;
}


static void oio_queue_accept(oio_accept_req *areq, oio_handle *handle) {
  BOOL success;
  DWORD bytes;

  areq->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (areq->socket == INVALID_SOCKET) {
    oio_close_error(handle, oio_new_sys_error(WSAGetLastError()));
    return;
  }

  if (oio_set_socket_options(areq->socket) != 0) {
    closesocket(areq->socket);
    oio_close_error(handle, oio_last_error_);
    return;
  }

  /* Prepare the oio_req and OVERLAPPED structures. */
  assert(!(areq->req.flags & OIO_REQ_PENDING));
  areq->req.flags |= OIO_REQ_PENDING;
  memset(&areq->req.overlapped, 0, sizeof(areq->req.overlapped));

  success = pAcceptEx(handle->socket,
                      areq->socket,
                      (void*)&areq->buffer,
                      0,
                      sizeof(struct sockaddr_storage),
                      sizeof(struct sockaddr_storage),
                      &bytes,
                      &areq->req.overlapped);

  if (!success && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    /* destroy the preallocated client handle */
    closesocket(areq->socket);
    /* destroy ourselves */
    oio_close_error(handle, oio_last_error_);
    return;
  }

  handle->reqs_pending++;
  areq->req.flags |= OIO_REQ_PENDING;
}


int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb) {
  oio_accept_req* areq;
  oio_accept_req* reqs;
  int i;

  assert(backlog > 0);

  if (handle->accept_reqs != NULL) {
    /* Already listening. */
    oio_set_sys_error(WSAEALREADY);
    return -1;
  }

  reqs = (oio_accept_req*)malloc(sizeof(oio_accept_req) * backlog);
  if (!reqs) {
    oio_set_sys_error(ERROR_OUTOFMEMORY);
    return -1;
  }

  if (listen(handle->socket, backlog) == SOCKET_ERROR) {
    oio_set_sys_error(WSAGetLastError());
    free(reqs);
    return -1;
  }

  for (i = backlog, areq = reqs; i > 0; i--, areq++) {
    areq->socket = INVALID_SOCKET;
    oio_req_init((oio_req*)areq, handle, (void*)cb);
    areq->req.type = OIO_ACCEPT;
    oio_queue_accept(areq, handle);
  }

  handle->accept_reqs = (oio_accept_req*)reqs;

  return 0;
}


int oio_connect(oio_req* req, struct sockaddr* addr) {
  int addrsize;
  BOOL success;
  DWORD bytes;
  oio_handle* handle = req->handle;

  assert(!(req->flags & OIO_REQ_PENDING));

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
    if (!(handle->flags & OIO_HANDLE_BOUND) &&
        oio_bind(handle, (struct sockaddr*)&oio_addr_ip4_any_) < 0)
      return -1;
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
    assert(0);
    return -1;
  } else {
    assert(0);
    return -1;
  }

  memset(&req->overlapped, 0, sizeof(req->overlapped));
  req->type = OIO_CONNECT;

  success = pConnectEx(handle->socket,
                       addr,
                       addrsize,
                       NULL,
                       0,
                       &bytes,
                       &req->overlapped);

  if (!success && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  req->flags |= OIO_REQ_PENDING;
  handle->reqs_pending++;

  return 0;
}


int oio_write(oio_req *req, oio_buf* bufs, int bufcnt) {
  int result;
  DWORD bytes;
  oio_handle* handle = req->handle;

  assert(!(req->flags & OIO_REQ_PENDING));

  memset(&req->overlapped, 0, sizeof(req->overlapped));
  req->type = OIO_WRITE;

  result = WSASend(handle->socket,
                   (WSABUF*)bufs,
                   bufcnt,
                   &bytes,
                   0,
                   &req->overlapped,
                   NULL);
  if (result != 0 && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  req->flags |= OIO_REQ_PENDING;
  handle->reqs_pending++;

  return 0;
}


int oio_read(oio_req *req, oio_buf* bufs, int bufcnt) {
  int result;
  DWORD bytes, flags;
  oio_handle* handle = req->handle;

  assert(!(req->flags & OIO_REQ_PENDING));

  memset(&req->overlapped, 0, sizeof(req->overlapped));
  req->type = OIO_READ;

  flags = 0;
  result = WSARecv(handle->socket,
                   (WSABUF*)bufs,
                   bufcnt,
                   &bytes,
                   &flags,
                   &req->overlapped,
                   NULL);
  if (result != 0 && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  req->flags |= OIO_REQ_PENDING;
  handle->reqs_pending++;

  return 0;
}


static int oio_timer_compare(oio_req *a, oio_req* b) {
  if (a->due < b->due)
    return -1;
  if (a->due > b->due)
    return 1;
  if ((intptr_t)a < (intptr_t)b)
    return -1;
  if ((intptr_t)a > (intptr_t)b)
    return 1;
  return 0;
}


RB_GENERATE_STATIC(oio_timer_s, oio_req_s, tree_entry, oio_timer_compare);


int oio_timeout(oio_req* req, int64_t timeout) {
  assert(!(req->flags & OIO_REQ_PENDING));

  req->type = OIO_TIMEOUT;

  req->due = oio_now_ + timeout;
  if (RB_INSERT(oio_timer_s, &oio_timers_, req) != NULL) {
    oio_set_sys_error(ERROR_INVALID_DATA);
    return -1;
  }

  oio_refs_++;
  req->flags |= OIO_REQ_PENDING;
  return 0;
}


void oio_update_time() {
  LARGE_INTEGER counter;

  if (!QueryPerformanceCounter(&counter))
    oio_fatal_error(GetLastError(), "QueryPerformanceCounter");

  oio_now_ = counter.QuadPart / oio_ticks_per_msec_;
}


int64_t oio_now() {
  return oio_now_;
}


static void oio_poll() {
  BOOL success;
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* overlapped;
  oio_req* req;
  oio_accept_req *accept_req;
  oio_handle* handle;
  DWORD timeout;
  int64_t delta;

  /* Call all pending close callbacks. */
  /* TODO: ugly, fixme. */
  oio_call_close_cbs();
  if (oio_refs_ == 0)
    return;

  oio_update_time();

  /* Check if there are any running timers */
  req = RB_MIN(oio_timer_s, &oio_timers_);
  if (req) {
    delta = req->due - oio_now_;
    if (delta >= UINT_MAX) {
      /* Can't have a timeout greater than UINT_MAX, and a timeout value of */
      /* UINT_MAX means infinite, so that's no good either. */
      timeout = UINT_MAX - 1;
    } else if (delta < 0) {
      /* Negative timeout values are not allowed */
      timeout = 0;
    } else {
      timeout = (DWORD)delta;
    }
  } else {
    /* No timers */
    timeout = INFINITE;
  }

  success = GetQueuedCompletionStatus(oio_iocp_,
                                      &bytes,
                                      &key,
                                      &overlapped,
                                      timeout);

  /* Call timer callbacks */
  oio_update_time();
  for (req = RB_MIN(oio_timer_s, &oio_timers_);
       req != NULL && req->due <= oio_now_;
       req = RB_MIN(oio_timer_s, &oio_timers_)) {
    RB_REMOVE(oio_timer_s, &oio_timers_, req);
    req->flags &= ~OIO_REQ_PENDING;
    oio_refs_--;
    ((oio_timer_cb)req->cb)(req, req->due - oio_now_, 0);
  }

  /* Only if a iocp package was dequeued... */
  if (overlapped) {
    req = oio_overlapped_to_req(overlapped);
    handle = req->handle;

    /* Mark the request non-pending */
    req->flags &= ~OIO_REQ_PENDING;

    switch (req->type) {
      case OIO_WRITE:
        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        if (!success) {
          oio_set_sys_error(GetLastError());
          oio_close_error(handle, oio_last_error_);
        }
        if (req->cb) {
          ((oio_write_cb)req->cb)(req, success ? 0 : -1);
        }
        break;

      case OIO_READ:
        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        if (!success) {
          oio_set_sys_error(GetLastError());
          oio_close_error(handle, oio_last_error_);
        }
        if (req->cb) {
          ((oio_read_cb)req->cb)(req, bytes, success ? 0 : -1);
        }
        break;

      case OIO_ACCEPT:
        accept_req = (oio_accept_req*)req;
        assert(accept_req->socket != INVALID_SOCKET);
        assert(handle->accepted_socket == INVALID_SOCKET);

        handle->accepted_socket = accept_req->socket;

        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        if (success) {
          if (setsockopt(handle->accepted_socket,
                         SOL_SOCKET,
                         SO_UPDATE_ACCEPT_CONTEXT,
                         (char*)&handle->socket,
                         sizeof(handle->socket)) == 0) {
            if (req->cb) {
              ((oio_accept_cb)req->cb)(handle);
            }
          }
        }

        /* accept_cb should call oio_accept_handle which sets data->socket */
        /* to INVALID_SOCKET. */
        /* Errorneous accept is ignored if the listen socket is still healthy. */
        if (handle->accepted_socket != INVALID_SOCKET) {
          closesocket(handle->accepted_socket);
          handle->accepted_socket = INVALID_SOCKET;
        }

        /* Queue another accept */
        if (!handle->flags & OIO_HANDLE_CLOSING)
          oio_queue_accept(accept_req, handle);
        break;

      case OIO_CONNECT:
        if (req->cb) {
          success = GetOverlappedResult(handle->handle,
                                        overlapped,
                                        &bytes,
                                        FALSE);
          if (success) {
            if (setsockopt(handle->socket,
                           SOL_SOCKET,
                           SO_UPDATE_CONNECT_CONTEXT,
                           NULL,
                           0) == 0) {
              ((oio_connect_cb)req->cb)(req, 0);
            } else {
              oio_set_sys_error(WSAGetLastError());
              ((oio_connect_cb)req->cb)(req, -1);
            }
          } else {
            oio_set_sys_error(WSAGetLastError());
            ((oio_connect_cb)req->cb)(req, -1);
          }
        }
        break;
    }

    /* The number of pending requests is now down by one */
    handle->reqs_pending--;

    /* Queue the handle's close callback if it is closing and there are no */
    /* more pending requests. */
    if (handle->flags & OIO_HANDLE_CLOSING &&
        handle->reqs_pending == 0) {
      oio_close_ready(handle);
    }
  } /* if (overlapped) */
}


int oio_run() {
  while (oio_refs_ > 0) {
    oio_poll();
  }
  assert(oio_refs_ == 0);
  return 0;
}
