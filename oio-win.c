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
               const struct sockaddr* name,
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
               LPSOCKADDR* LocalSockaddr,
               LPINT LocalSockaddrLength,
               LPSOCKADDR* RemoteSockaddr,
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
 * Pointers to winsock extension functions to be retrieved dynamically
 */
static LPFN_CONNECTEX               pConnectEx;
static LPFN_ACCEPTEX                pAcceptEx;
static LPFN_GETACCEPTEXSOCKADDRS    pGetAcceptExSockAddrs;
static LPFN_DISCONNECTEX            pDisconnectEx;
static LPFN_TRANSMITFILE            pTransmitFile;


/*
 * Private oio_handle flags
 */
#define OIO_HANDLE_CLOSING          0x0001
#define OIO_HANDLE_CLOSED           0x0002
#define OIO_HANDLE_BOUND            0x0004
#define OIO_HANDLE_LISTENING        0x0008
#define OIO_HANDLE_CONNECTION       0x0010
#define OIO_HANDLE_CONNECTED        0x0020
#define OIO_HANDLE_READING          0x0040
#define OIO_HANDLE_EOF              0x0080
#define OIO_HANDLE_SHUTTING         0x0100
#define OIO_HANDLE_SHUT             0x0200
#define OIO_HANDLE_ENDGAME_QUEUED   0x0400
#define OIO_HANDLE_BIND_ERROR       0x1000

/*
 * Private oio_req flags.
 */
/* The request is currently queued. */
#define OIO_REQ_PENDING      0x01


/* Binary tree used to keep the list of timers sorted. */
static int oio_timer_compare(oio_req* t1, oio_req* t2);
RB_HEAD(oio_timer_s, oio_req_s);
RB_PROTOTYPE_STATIC(oio_timer_s, oio_req_s, tree_entry, oio_timer_compare);

/* The head of the timers tree */
static struct oio_timer_s oio_timers_ = RB_INITIALIZER(oio_timers_);


/* Head of a single-linked list of closed handles */
static oio_handle* oio_endgame_handles_ = NULL;


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

/* Error message string */
static char* oio_err_str_ = NULL;

/* Global alloc function */
oio_alloc_cb oio_alloc_ = NULL;


/* Reference count that keeps the event loop alive */
static int oio_refs_ = 0;


/* Ip address used to bind to any port at any interface */
static struct sockaddr_in oio_addr_ip4_any_;


/* A zero-size buffer for use by oio_read */
static char oio_zero_[] = "";


/*
 * Display an error message and abort the event loop.
 */
static void oio_fatal_error(const int errorno, const char* syscall) {
  char* buf = NULL;
  const char* errmsg;

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


char* oio_strerror(oio_err err) {
  if (oio_err_str_ != NULL) {
    LocalFree((void*) oio_err_str_);
  }

  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err.sys_errno_,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&oio_err_str_, 0, NULL);

  if (oio_err_str_) {
    return oio_err_str_;
  } else {
    return "Unknown error";
  }
}


static oio_err_code oio_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case ERROR_SUCCESS:                     return OIO_OK;
    case ERROR_NOACCESS:                    return OIO_EACCESS;
    case WSAEACCES:                         return OIO_EACCESS;
    case ERROR_ADDRESS_ALREADY_ASSOCIATED:  return OIO_EADDRINUSE;
    case WSAEADDRINUSE:                     return OIO_EADDRINUSE;
    case WSAEADDRNOTAVAIL:                  return OIO_EADDRNOTAVAIL;
    case WSAEALREADY:                       return OIO_EALREADY;
    case ERROR_CONNECTION_REFUSED:          return OIO_ECONNREFUSED;
    case WSAECONNREFUSED:                   return OIO_ECONNREFUSED;
    case WSAEFAULT:                         return OIO_EFAULT;
    case WSAEINVAL:                         return OIO_EINVAL;
    case ERROR_TOO_MANY_OPEN_FILES:         return OIO_EMFILE;
    case WSAEMFILE:                         return OIO_EMFILE;
    case ERROR_OUTOFMEMORY:                 return OIO_ENOMEM;
    default:                                return OIO_UNKNOWN;
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


void oio_init(oio_alloc_cb alloc_cb) {
  const GUID wsaid_connectex            = WSAID_CONNECTEX;
  const GUID wsaid_acceptex             = WSAID_ACCEPTEX;
  const GUID wsaid_getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
  const GUID wsaid_disconnectex         = WSAID_DISCONNECTEX;
  const GUID wsaid_transmitfile         = WSAID_TRANSMITFILE;

  WSADATA wsa_data;
  int errorno;
  LARGE_INTEGER timer_frequency;
  SOCKET dummy;

  oio_alloc_ = alloc_cb;

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


void oio_req_init(oio_req* req, oio_handle* handle, void* cb) {
  req->type = OIO_UNKNOWN_REQ;
  req->flags = 0;
  req->handle = handle;
  req->cb = cb;
}


static oio_req* oio_overlapped_to_req(OVERLAPPED* overlapped) {
  return CONTAINING_RECORD(overlapped, oio_req, overlapped);
}


static int oio_tcp_init_socket(oio_handle* handle, oio_close_cb close_cb,
    void* data, SOCKET socket) {
  DWORD yes = 1;

  handle->socket = socket;
  handle->close_cb = close_cb;
  handle->data = data;
  handle->type = OIO_TCP;
  handle->flags = 0;
  handle->reqs_pending = 0;
  handle->error = oio_ok_;
  handle->accept_socket = INVALID_SOCKET;

  oio_req_init(&(handle->read_accept_req), handle, NULL);

  /* Set the socket to nonblocking mode */
  if (ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

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

  oio_refs_++;

  return 0;
}


static void oio_tcp_init_connection(oio_handle* handle) {
  handle->flags |= OIO_HANDLE_CONNECTION;
  handle->write_reqs_pending = 0;
}


int oio_tcp_init(oio_handle* handle, oio_close_cb close_cb,
    void* data) {
  SOCKET sock;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (handle->socket == INVALID_SOCKET) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  if (oio_tcp_init_socket(handle, close_cb, data, sock) == -1) {
    closesocket(sock);
    return -1;
  }

  return 0;
}


static void oio_tcp_endgame(oio_handle* handle) {
  oio_err err;
  int status;

  if (handle->flags & OIO_HANDLE_SHUTTING &&
      !(handle->flags & OIO_HANDLE_SHUT) &&
      handle->write_reqs_pending == 0) {

    if (shutdown(handle->socket, SD_SEND) != SOCKET_ERROR) {
      status = 0;
      handle->flags |= OIO_HANDLE_SHUT;
    } else {
      status = -1;
      err = oio_new_sys_error(WSAGetLastError());
    }
    if (handle->shutdown_req->cb) {
      handle->shutdown_req->flags &= ~OIO_REQ_PENDING;
      if (status == -1) {
        oio_last_error_ = err;
      }
      ((oio_shutdown_cb)handle->shutdown_req->cb)(handle->shutdown_req, status);
    }
    handle->reqs_pending--;
  }

  if (handle->flags & OIO_HANDLE_EOF &&
      handle->flags & OIO_HANDLE_SHUT &&
      !(handle->flags & OIO_HANDLE_CLOSING)) {
    /* Because oio_close will add the handle to the endgame_handles list, */
    /* return here and call the close cb the next time. */
    oio_close(handle);
    return;
  }

  if (handle->flags & OIO_HANDLE_CLOSING &&
      !(handle->flags & OIO_HANDLE_CLOSED) &&
      handle->reqs_pending == 0) {
    handle->flags |= OIO_HANDLE_CLOSED;

    if (handle->close_cb) {
      oio_last_error_ = handle->error;
      handle->close_cb(handle, handle->error.code == OIO_OK ? 0 : 1);
    }

    oio_refs_--;
  }
}


static void oio_call_endgames() {
  oio_handle* handle;

  while (oio_endgame_handles_) {
    handle = oio_endgame_handles_;
    oio_endgame_handles_ = handle->endgame_next;

    handle->flags &= ~OIO_HANDLE_ENDGAME_QUEUED;

    switch (handle->type) {
      case OIO_TCP:
        oio_tcp_endgame(handle);
        break;

      default:
        assert(0);
        break;
    }
  }
}


static void oio_want_endgame(oio_handle* handle) {
  if (!(handle->flags & OIO_HANDLE_ENDGAME_QUEUED)) {
    handle->flags |= OIO_HANDLE_ENDGAME_QUEUED;

    handle->endgame_next = oio_endgame_handles_;
    oio_endgame_handles_ = handle;
  }
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
      oio_want_endgame(handle);
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


struct sockaddr_in oio_ip4_addr(char* ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int oio_bind(oio_handle* handle, struct sockaddr* addr) {
  int addrsize;
  DWORD err;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
  } else {
    oio_set_sys_error(WSAEFAULT);
    return -1;
  }

  if (bind(handle->socket, addr, addrsize) == SOCKET_ERROR) {
    err = WSAGetLastError();
    if (err == WSAEADDRINUSE) {
      /* Some errors are not to be reported until connect() or listen() */
      handle->error = oio_new_sys_error(err);
      handle->flags |= OIO_HANDLE_BIND_ERROR;
    } else {
      oio_set_sys_error(err);
      return -1;
    }
  }

  handle->flags |= OIO_HANDLE_BOUND;

  return 0;
}


static void oio_queue_accept(oio_handle* handle) {
  oio_req* req;
  BOOL success;
  DWORD bytes;
  SOCKET accept_socket;

  assert(handle->flags & OIO_HANDLE_LISTENING);
  assert(handle->accept_socket == INVALID_SOCKET);

  accept_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (accept_socket == INVALID_SOCKET) {
    oio_close_error(handle, oio_new_sys_error(WSAGetLastError()));
    return;
  }

  /* Prepare the oio_req and OVERLAPPED structures. */
  req = &handle->read_accept_req;
  assert(!(req->flags & OIO_REQ_PENDING));
  req->type = OIO_ACCEPT;
  req->flags |= OIO_REQ_PENDING;
  memset(&(req->overlapped), 0, sizeof(req->overlapped));

  success = pAcceptEx(handle->socket,
                      accept_socket,
                      (void*)&handle->accept_buffer,
                      0,
                      sizeof(struct sockaddr_storage),
                      sizeof(struct sockaddr_storage),
                      &bytes,
                      &req->overlapped);

  if (!success && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    /* destroy the preallocated client handle */
    closesocket(accept_socket);
    /* destroy ourselves */
    oio_close_error(handle, oio_last_error_);
    return;
  }

  handle->accept_socket = accept_socket;

  handle->reqs_pending++;
  req->flags |= OIO_REQ_PENDING;
}


void oio_queue_read(oio_handle* handle) {
  oio_req *req;
  oio_buf buf;
  int result;
  DWORD bytes, flags;

  assert(handle->flags & OIO_HANDLE_READING);

  req = &handle->read_accept_req;
  assert(!(req->flags & OIO_REQ_PENDING));
  memset(&req->overlapped, 0, sizeof(req->overlapped));
  req->type = OIO_READ;

  buf.base = (char*) &oio_zero_;
  buf.len = 0;

  flags = 0;
  result = WSARecv(handle->socket,
                   (WSABUF*)&buf,
                   1,
                   &bytes,
                   &flags,
                   &req->overlapped,
                   NULL);
  if (result != 0 && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_set_sys_error(WSAGetLastError());
    oio_close_error(handle, oio_last_error_);
    return;
  }

  req->flags |= OIO_REQ_PENDING;
  handle->reqs_pending++;
}


int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb) {
  assert(backlog > 0);

  if (handle->flags & OIO_HANDLE_BIND_ERROR) {
    oio_last_error_ = handle->error;
    return -1;
  }

  if (handle->flags & OIO_HANDLE_LISTENING ||
      handle->flags & OIO_HANDLE_READING) {
    /* Already listening. */
    oio_set_sys_error(WSAEALREADY);
    return -1;
  }

  if (listen(handle->socket, backlog) == SOCKET_ERROR) {
    oio_set_sys_error(WSAGetLastError());
    return -1;
  }

  handle->flags |= OIO_HANDLE_LISTENING;
  handle->accept_cb = cb;

  oio_queue_accept(handle);

  return 0;
}


int oio_accept(oio_handle* server, oio_handle* client,
    oio_close_cb close_cb, void* data) {
  int rv = 0;

  if (server->accept_socket == INVALID_SOCKET) {
    oio_set_sys_error(WSAENOTCONN);
    return -1;
  }

  if (oio_tcp_init_socket(client, close_cb, data, server->accept_socket) == -1) {
    oio_fatal_error(oio_last_error_.sys_errno_, "init");
    closesocket(server->accept_socket);
    rv = -1;
  }

  oio_tcp_init_connection(client);

  server->accept_socket = INVALID_SOCKET;

  if (!(server->flags & OIO_HANDLE_CLOSING)) {
    oio_queue_accept(server);
  }

  return rv;
}


int oio_read_start(oio_handle* handle, oio_read_cb cb) {
  if (!(handle->flags & OIO_HANDLE_CONNECTION)) {
    oio_set_sys_error(WSAEINVAL);
    return -1;
  }

  if (handle->flags & OIO_HANDLE_READING) {
    oio_set_sys_error(WSAEALREADY);
    return -1;
  }

  if (handle->flags & OIO_HANDLE_EOF) {
    oio_set_sys_error(WSAESHUTDOWN);
    return -1;
  }

  handle->flags |= OIO_HANDLE_READING;
  handle->read_cb = cb;

  /* If reading was stopped and then started again, there could stell be a */
  /* read request pending. */
  if (!(handle->read_accept_req.flags & OIO_REQ_PENDING))
    oio_queue_read(handle);

  return 0;
}


int oio_read_stop(oio_handle* handle) {
  handle->flags &= ~OIO_HANDLE_READING;

  return 0;
}


int oio_connect(oio_req* req, struct sockaddr* addr) {
  int addrsize;
  BOOL success;
  DWORD bytes;
  oio_handle* handle = req->handle;

  assert(!(req->flags & OIO_REQ_PENDING));

  if (handle->flags & OIO_HANDLE_BIND_ERROR) {
    oio_last_error_ = handle->error;
    return -1;
  }

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


int oio_write(oio_req* req, oio_buf* bufs, int bufcnt) {
  int result;
  DWORD bytes;
  oio_handle* handle = req->handle;

  assert(!(req->flags & OIO_REQ_PENDING));

  if (!(req->handle->flags & OIO_HANDLE_CONNECTION)) {
    oio_set_sys_error(WSAEINVAL);
    return -1;
  }

  if (req->handle->flags & OIO_HANDLE_SHUTTING) {
    oio_set_sys_error(WSAESHUTDOWN);
    return -1;
  }

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
  handle->write_reqs_pending++;

  return 0;
}


int oio_shutdown(oio_req* req) {
  oio_handle* handle = req->handle;
  int status = 0;

  if (!(req->handle->flags & OIO_HANDLE_CONNECTION)) {
    oio_set_sys_error(WSAEINVAL);
    return -1;
  }

  if (handle->flags & OIO_HANDLE_SHUTTING) {
    oio_set_sys_error(WSAESHUTDOWN);
    return -1;
  }

  req->type = OIO_SHUTDOWN;
  req->flags |= OIO_REQ_PENDING;

  handle->flags |= OIO_HANDLE_SHUTTING;
    handle->shutdown_req = req;
  handle->reqs_pending++;

  oio_want_endgame(handle);

  return 0;
}


static int oio_timer_compare(oio_req* a, oio_req* b) {
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
  oio_handle* handle;
  oio_buf buf;
  DWORD timeout;
  DWORD flags;
  DWORD err;
  int64_t delta;
  int status;

  /* Call all pending close callbacks. */
  /* TODO: ugly, fixme. */
  oio_call_endgames();
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
        handle->write_reqs_pending--;
        if (success &&
            handle->write_reqs_pending == 0 &&
            handle->flags & OIO_HANDLE_SHUTTING) {
          oio_want_endgame(handle);
        }
        break;

      case OIO_READ:
        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        if (!success) {
          oio_set_sys_error(GetLastError());
          oio_close_error(handle, oio_last_error_);
        }
        while (handle->flags & OIO_HANDLE_READING) {
          buf = oio_alloc_(handle, 65536);
          assert(buf.len > 0);
          flags = 0;
          if (WSARecv(handle->socket,
                      (WSABUF*)&buf,
                      1,
                      &bytes,
                      &flags,
                      NULL,
                      NULL) != SOCKET_ERROR) {
            if (bytes > 0) {
              /* Successful read */
              ((oio_read_cb)handle->read_cb)(handle, bytes, buf);
              /* Read again only if bytes == buf.len */
              if (bytes < buf.len) {
                break;
              }
            } else {
              /* Connection closed */
              handle->flags &= ~OIO_HANDLE_READING;
              handle->flags |= OIO_HANDLE_EOF;
              oio_last_error_.code = OIO_EOF;
              oio_last_error_.sys_errno_ = ERROR_SUCCESS;
              ((oio_read_cb)handle->read_cb)(handle, -1, buf);
              oio_want_endgame(handle);
              break;
            }
          } else {
            err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
              /* 0-byte read */
              ((oio_read_cb)handle->read_cb)(handle, 0, buf);
            } else {
              /* Ouch! serious error. */
              oio_set_sys_error(err);
              oio_close_error(handle, oio_last_error_);
            }
            break;
          }
        }
        /* Post another 0-read if still reading and not closing */
        if (!(handle->flags & OIO_HANDLE_CLOSING) &&
            !(handle->flags & OIO_HANDLE_EOF) &&
            handle->flags & OIO_HANDLE_READING) {
          oio_queue_read(handle);
        }
        break;

      case OIO_ACCEPT:
        assert(handle->accept_socket != INVALID_SOCKET);

        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        success = success && (setsockopt(handle->accept_socket,
                                         SOL_SOCKET,
                                         SO_UPDATE_ACCEPT_CONTEXT,
                                         (char*)&handle->socket,
                                         sizeof(handle->socket)) == 0);

        if (success) {
          if (handle->accept_cb) {
            ((oio_accept_cb)handle->accept_cb)(handle);
          }
        } else {
          /* Errorneous accept is ignored if the listen socket is still healthy. */
          closesocket(handle->accept_socket);
          if (!(handle->flags & OIO_HANDLE_CLOSING)) {
            oio_queue_accept(handle);
          }
        }
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
              oio_tcp_init_connection(handle);
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
      oio_want_endgame(handle);
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
