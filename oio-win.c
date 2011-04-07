
#include "oio.h"
#include <assert.h>
#include <malloc.h>
#include <stdio.h>


/*
 * Guids and typedefs for winsock extension functions
 * Mingw32 doesn't have these :-(
 */
#ifndef WSAID_ACCEPTEX
# define WSAID_ACCEPTEX \
         {0xb5367df1, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}};

# define WSAID_CONNECTEX \
         {0x25a207b9, 0xddf3, 0x4660, {0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e}};

# define WSAID_GETACCEPTEXSOCKADDRS \
         {0xb5367df2, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}};

# define WSAID_DISCONNECTEX \
         {0x7fda2e11, 0x8630, 0x436f, {0xa0, 0x31, 0xf5, 0x36, 0xa6, 0xee, 0xc1, 0x57}};

# define WSAID_TRANSMITFILE \
         {0xb5367df0, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}};

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
 * Private oio_handle flags
 */
#define OIO_HANDLE_CLOSING   0x01
#define OIO_HANDLE_CLOSED    0x03

/*
 * Private oio_req flags.
 */
/* The request is currently queued. */
#define OIO_REQ_PENDING      0x01

/*
 * Pointers to winsock extension functions that have to be retrieved dynamically
 */
LPFN_CONNECTEX            pConnectEx;
LPFN_ACCEPTEX             pAcceptEx;
LPFN_GETACCEPTEXSOCKADDRS pGetAcceptExSockAddrs;
LPFN_DISCONNECTEX         pDisconnectEx;
LPFN_TRANSMITFILE         pTransmitFile;

/*
 * Global I/O completion port
 */
HANDLE oio_iocp_;


/* Global error code */
int oio_errno_;


/* Reference count that keeps the event loop alive */
int oio_refs_ = 0;


/*
 * Display an error message and abort the event loop.
 */
void oio_fatal_error(const int errorno, const char *syscall) {
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


/*
 * Retrieves the pointer to a winsock extension function.
 */
void oio_get_extension_function(SOCKET socket, GUID guid, void **target) {
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
    oio_fatal_error(WSAGetLastError(), "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
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
  SOCKET dummy;

  /* Initialize winsock */
  errorno = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (errorno != 0) {
    oio_fatal_error(errorno, "WSAStartup");
  }


  /* Retrieve the needed winsock extension function pointers. */
  dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (dummy == INVALID_SOCKET) {
    oio_fatal_error(WSAGetLastError(), "socket");
  }

  oio_get_extension_function(dummy, wsaid_connectex,            (void**)&pConnectEx           );
  oio_get_extension_function(dummy, wsaid_acceptex,             (void**)&pAcceptEx            );
  oio_get_extension_function(dummy, wsaid_getacceptexsockaddrs, (void**)&pGetAcceptExSockAddrs);
  oio_get_extension_function(dummy, wsaid_disconnectex,         (void**)&pDisconnectEx        );
  oio_get_extension_function(dummy, wsaid_transmitfile,         (void**)&pTransmitFile        );

  if (closesocket(dummy) == SOCKET_ERROR) {
    oio_fatal_error(WSAGetLastError(), "closesocket");
  }

  /* Create an I/O completion port */
  oio_iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (oio_iocp_ == NULL) {
    oio_fatal_error(GetLastError(), "CreateIoCompletionPort");
  }
}


void oio_req_init(oio_req* req, oio_handle* handle, void *cb) {
  req->type = OIO_UNKNOWN_REQ;
  req->flags = 0;
  req->handle = handle;
  req->cb = cb;
}


oio_req* oio_overlapped_to_req(OVERLAPPED* overlapped) {
  return CONTAINING_RECORD(overlapped, oio_req, overlapped);
}


int oio_set_socket_options(SOCKET socket) {
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
    oio_errno_ = GetLastError();
    return -1;
  }

  /* Associate it with the I/O completion port. */
  /* Use oio_handle pointer as completion key. */
  if (CreateIoCompletionPort((HANDLE)socket,
                             oio_iocp_,
                             (ULONG_PTR)socket,
                             0) == NULL) {
    oio_errno_ = GetLastError();
    return -1;
  }

  return 0;
}


int oio_tcp_handle_init(oio_handle *handle, oio_close_cb close_cb, void* data) {
  handle->close_cb = close_cb;
  handle->data = data;
  handle->type = OIO_TCP;
  handle->flags = 0;
  handle->reqs_pending = 0;
  handle->error = 0;
  handle->accept_data = NULL;

  handle->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (handle->socket == INVALID_SOCKET) {
    oio_errno_ = WSAGetLastError();
    return -1;
  }

  if (oio_set_socket_options(handle->socket) != 0) {
    closesocket(handle->socket);
    return -1;
  }

  oio_refs_++;

  return 0;
}


int oio_tcp_handle_accept(oio_handle* server, oio_handle* client, oio_close_cb close_cb, void* data) {
  if (!server->accept_data ||
      server->accept_data->socket == INVALID_SOCKET) {
    oio_errno_ = WSAENOTCONN;
    return -1;
  }

  client->close_cb = close_cb;
  client->data = data;
  client->type = OIO_TCP;
  client->socket = server->accept_data->socket;
  client->flags = 0;
  client->reqs_pending = 0;
  client->error = 0;
  client->accept_data = NULL;

  server->accept_data->socket = INVALID_SOCKET;
  oio_refs_++;

  return 0;
}


int oio_close_error(oio_handle* handle, oio_err e) {
  oio_req *req;

  if (handle->flags & OIO_HANDLE_CLOSING)

    return 0;

  handle->error = e;

  switch (handle->type) {
    case OIO_TCP:
      closesocket(handle->socket);
      if (handle->reqs_pending == 0) {
        /* If there are no operations queued for this socket, queue one */
        /* manually, so oio_poll will call close_cb. */
        req = (oio_req*)malloc(sizeof(*req));
        req->handle = handle;
        req->type = OIO_CLOSE;
        req->flags = 0;
        if (!PostQueuedCompletionStatus(oio_iocp_, 0, (ULONG_PTR)handle, &req->overlapped))
          oio_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
        req->flags |= OIO_REQ_PENDING;
        handle->reqs_pending++;
      }

      /* After all packets to come out, oio_poll will call close_cb. */
      handle->flags |= OIO_HANDLE_CLOSING;
      return 0;

    default:
      /* Not supported */
      assert(0);
      return -1;
  }
}


int oio_close(oio_handle* handle) {
  return oio_close_error(handle, 0);
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
    oio_errno_ = WSAGetLastError();
    return -1;
  }

  return 0;
}


void oio_queue_accept(oio_handle *handle) {
  oio_accept_data* data;
  BOOL success;
  DWORD bytes;

  data = handle->accept_data;
  assert(data != NULL);

  data->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (data->socket == INVALID_SOCKET) {
    oio_close_error(handle, WSAGetLastError());
    return;
  }

  if (oio_set_socket_options(data->socket) != 0) {
    closesocket(data->socket);
    oio_close_error(handle, oio_errno_);
    return;
  }

  /* Prepare the oio_req and OVERLAPPED structures. */
  assert(!(data->req.flags & OIO_REQ_PENDING));
  data->req.flags |= OIO_REQ_PENDING;
  memset(&data->req.overlapped, 0, sizeof(data->req.overlapped));

  success = pAcceptEx(handle->socket,
                      data->socket,
                      (void*)&data->buffer,
                      0,
                      sizeof(struct sockaddr_storage),
                      sizeof(struct sockaddr_storage),
                      &bytes,
                      &data->req.overlapped);

  if (!success && WSAGetLastError() != ERROR_IO_PENDING) {
    oio_errno_ = WSAGetLastError();
    /* destroy the preallocated client handle */
    closesocket(data->socket);
    /* destroy ourselves */
    oio_close_error(handle, oio_errno_);
    return;
  }

  handle->reqs_pending++;
  data->req.flags |= OIO_REQ_PENDING;
}


int oio_listen(oio_handle* handle, int backlog, oio_accept_cb cb) {
  oio_accept_data *data;

  if (handle->accept_data != NULL) {
    /* Already listening. */
    oio_errno_ = WSAEALREADY;
    return -1;
  }

  data = (oio_accept_data*)malloc(sizeof(*data));
  if (!data) {
    oio_errno_ = WSAENOBUFS;
    return -1;
  }
  data->socket = INVALID_SOCKET;
  oio_req_init(&data->req, handle, (void*)cb);
  data->req.type = OIO_ACCEPT;

  if (listen(handle->socket, backlog) == SOCKET_ERROR) {
    oio_errno_ = WSAGetLastError();
    free(data);
    return -1;
  }

  handle->accept_data = data;

  oio_queue_accept(handle);

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
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
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
    oio_errno_ = WSAGetLastError();
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
    oio_errno_ = WSAGetLastError();
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
    oio_errno_ = WSAGetLastError();
    return -1;
  }

  req->flags |= OIO_REQ_PENDING;
  handle->reqs_pending++;

  return 0;
}


int oio_write2(oio_req *req, const char* msg) {
  oio_buf buf;
  oio_handle* handle = req->handle;

  buf.base = (char*)msg;
  buf.len = strlen(msg);

  return oio_write(req, &buf, 1);
}


oio_err oio_last_error() {
  return oio_errno_;
}


void oio_poll() {
  BOOL success;
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* overlapped;
  oio_req* req;
  oio_handle* handle;
  oio_accept_data *data;

  success = GetQueuedCompletionStatus(oio_iocp_,
                                      &bytes,
                                      &key,
                                      &overlapped,
                                      INFINITE);

  if (!success && !overlapped)
    oio_fatal_error(GetLastError(), "GetQueuedCompletionStatus");

  req = oio_overlapped_to_req(overlapped);
  handle = req->handle;

  /* Mark the request non-pending */
  req->flags &= ~OIO_REQ_PENDING;
  handle->reqs_pending--;

  /* If the related socket got closed in the meantime, disregard this */
  /* result. If this is the last request pending, call the handle's close callback. */
  if (handle->flags & OIO_HANDLE_CLOSING) {
    if (handle->reqs_pending == 0) {
      handle->flags |= OIO_HANDLE_CLOSED;
      if (handle->accept_data) {
        if (handle->accept_data) {
          if (handle->accept_data->socket) {
            closesocket(handle->accept_data->socket);
          }
          free(handle->accept_data);
          handle->accept_data = NULL;
        }
      }
      if (handle->close_cb) {
        handle->close_cb(handle, handle->error);
      }
      oio_refs_--;
    }
    return;
  }

  switch (req->type) {
    case OIO_WRITE:
      success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
      if (!success) {
        oio_close_error(handle, GetLastError());
      } else if (req->cb) {
        ((oio_write_cb)req->cb)(req);
      }
      return;

    case OIO_READ:
      success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
      if (!success) {
        oio_close_error(handle, GetLastError());
      } else if (req->cb) {
        ((oio_read_cb)req->cb)(req, bytes);
      }
      break;

    case OIO_ACCEPT:
      data = handle->accept_data;
      assert(data != NULL);
      assert(data->socket != INVALID_SOCKET);

      success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
      if (success && req->cb) {
        ((oio_accept_cb)req->cb)(handle);
      }

      /* accept_cb should call oio_accept_handle which sets data->socket */
      /* to INVALID_SOCKET. */
      /* Just ignore failed accept if the listen socket is still healthy. */
      if (data->socket != INVALID_SOCKET) {
        closesocket(handle->socket);
        data->socket = INVALID_SOCKET;
      }

      /* Queue another accept */
      oio_queue_accept(handle);
      return;

    case OIO_CONNECT:
      if (req->cb) {
        success = GetOverlappedResult(handle->handle, overlapped, &bytes, FALSE);
        if (success) {
          ((oio_connect_cb)req->cb)(req, 0);
        } else {
          ((oio_connect_cb)req->cb)(req, GetLastError());
        }
      }
      return;

    case OIO_CLOSE:
      /* Should never get here */
      assert(0);
  }
}


int oio_run() {
  while (oio_refs_ > 0) {
    oio_poll();
  }
  assert(oio_refs_ == 0);
  return 0;
}
