
#include "ol.h"
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
 * Private ol_req flags. 
 */
/* The request is currently queued. */
#define OL_REQ_PENDING  0x01 
/* When STRAY is set, that means that the owner of the containing ol_req */
/* struct was destroyed while the old_req was queued to an iocp, so its */
/* memory could not be freed. The event loop will release the ol_req */
/* structure as soon as it is dequeued with GetQueuedCompletionStatus. */
#define OL_REQ_STRAY    0x02


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
HANDLE ol_iocp_;


/* Global error code */
int ol_errno_;


/*
 * Display an error message and abort the event loop.
 */
void ol_fatal_error(const int errorno, const char *syscall) {
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
void ol_get_extension_function(SOCKET socket, GUID guid, void **target) {
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
    ol_fatal_error(WSAGetLastError(), "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
  }
}


void ol_init() {
  const GUID wsaid_connectex            = WSAID_CONNECTEX;
  const GUID wsaid_acceptex             = WSAID_CONNECTEX;
  const GUID wsaid_getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
  const GUID wsaid_disconnectex         = WSAID_DISCONNECTEX;
  const GUID wsaid_transmitfile         = WSAID_TRANSMITFILE;

  WSADATA wsa_data;
  int errorno;
  SOCKET dummy;

  /* Initialize winsock */
  errorno = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (errorno != 0) {
    ol_fatal_error(errorno, "WSAStartup");
  }


  /* Retrieve the needed winsock extension function pointers. */
  dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (dummy == INVALID_SOCKET) {
    ol_fatal_error(WSAGetLastError(), "socket");
  }

  ol_get_extension_function(dummy, wsaid_connectex,            (void**)&pConnectEx           );
  ol_get_extension_function(dummy, wsaid_acceptex,             (void**)&pAcceptEx            );
  ol_get_extension_function(dummy, wsaid_getacceptexsockaddrs, (void**)&pGetAcceptExSockAddrs);
  ol_get_extension_function(dummy, wsaid_disconnectex,         (void**)&pDisconnectEx        );
  ol_get_extension_function(dummy, wsaid_transmitfile,         (void**)&pTransmitFile        );

  if (closesocket(dummy) == SOCKET_ERROR) {
    ol_fatal_error(WSAGetLastError(), "closesocket");
  }

  /* Create an I/O completion port */
  ol_iocp_ = CreateIoCompletionPort(NULL, NULL, 0, 0);
  if (ol_iocp_ == NULL) {
    ol_fatal_error(GetLastError(), "CreateIoCompletionPort");
  }
}


ol_req* ol_overlapped_to_req(OVERLAPPED* overlapped) {
  return CONTAINING_RECORD(overlapped, ol_req, _.overlapped);
}


int ol_set_socket_options(ol_handle *handle) {
  DWORD yes = 1;

  /* Set the SO_REUSEADDR option on the socket */
  /* If it fails, soit. */
  setsockopt(handle->_.socket, 
             SOL_SOCKET, 
             SO_REUSEADDR, 
             (char*)&yes, 
             sizeof(int));

  /* Make the socket non-inheritable */
  if (!SetHandleInformation(handle->_.handle, HANDLE_FLAG_INHERIT, 0)) {
    ol_errno_ = GetLastError();
    return -1;
  }

  /* Associate it with the I/O completion port. */
  /* Use ol_handle pointer as completion key. */
  if (CreateIoCompletionPort(handle->_.handle, 
                             ol_iocp_, 
                             (ULONG_PTR)handle, 
                             0) == NULL) {
    ol_errno_ = GetLastError();
    return -1;
  }

  return 0;
}


ol_handle* ol_tcp_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle;

  handle = (ol_handle*)calloc(sizeof(ol_handle), 1);
  handle->close_cb = close_cb;
  handle->data = data;
  handle->type = OL_TCP;

  handle->_.socket = socket(AF_INET, SOCK_STREAM, 0);
  if (handle->_.socket == INVALID_SOCKET) {
    ol_errno_ = WSAGetLastError();
    free(handle);
    return NULL;
  }

  if (ol_set_socket_options(handle) != 0) {
    closesocket(handle->_.socket);
    free(handle);
    return NULL;
  }
    
  return handle;
}


struct sockaddr_in ol_ip4_addr(char *ip, int port) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);

  return addr;
}


int ol_bind(ol_handle* handle, struct sockaddr* addr) {
  int addrsize;

  if (addr->sa_family == AF_INET) {
    addrsize = sizeof(struct sockaddr_in);
  } else if (addr->sa_family == AF_INET6) {
    addrsize = sizeof(struct sockaddr_in6);
  } else {
    assert(0);
    return -1;
  }

  if (bind(handle->_.socket, addr, addrsize) == SOCKET_ERROR) {
    ol_errno_ = WSAGetLastError();
    return -1;
  }

  return 0;
}


void ol_queue_accept(ol_handle *handle) {
  ol_handle* peer;
  void *buffer;
  ol_req *req;
  DWORD bytes;

  peer = ol_tcp_handle_new(NULL, NULL);
  if (peer == NULL) {
    /* Todo: report instead of dying */
    ol_fatal_error(ol_errno_, "AcceptEx");
  }

  /* AcceptEx specifies that the buffer must be big enough to at least hold */
  /* two socket addresses plus 32 bytes. */
  buffer = malloc(sizeof(struct sockaddr_storage) * 2 + 32);

  /* Prepare the ol_req and OVERLAPPED structures. */
  req = handle->_.accept_req;
  req->_.flags |= OL_REQ_PENDING;
  req->data = (void*)peer;
  memset(&req->_.overlapped, 0, sizeof(req->_.overlapped));

  if (!pAcceptEx(handle->_.socket, 
                 peer->_.socket, 
                 buffer, 
                 0, 
                 sizeof(struct sockaddr_storage), 
                 sizeof(struct sockaddr_storage), 
                 &bytes, 
                 &req->_.overlapped)) {
    if (WSAGetLastError() != ERROR_IO_PENDING) {
      ol_errno_ = WSAGetLastError();
      req->_.flags &= ~OL_REQ_PENDING;
      ol_close(peer);
      ol_free(peer);
      /* Todo: report instead of dying */
      ol_fatal_error(ol_errno_, "AcceptEx");
    }
  }
}



int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb) {
  ol_req* req;

  if (listen(handle->_.socket, backlog) == SOCKET_ERROR)
    return -1;

  handle->accept_cb = cb;
  req = (ol_req*)malloc(sizeof(handle->_.accept_req));
  handle->_.accept_req = req;
  handle->_.accept_req->type = OL_ACCEPT;
  
  ol_queue_accept(handle);
}


int ol_close(ol_handle* handle) {
  switch (handle->type) {
    case OL_TCP:
      if (handle->_.accept_req) {
        if (handle->_.accept_req->_.flags & OL_REQ_PENDING) {
          handle->_.accept_req->_.flags |= OL_REQ_STRAY;
        } else {
          free(handle->_.accept_req);
        }
      }
      if (closesocket(handle->_.socket) == SOCKET_ERROR)
        return -1;
      return 0;

    default:
      /* Not supported */
      assert(0);
      return -1;
  }
}


void ol_free(ol_handle* handle) {
  free(handle);
}


void ol_poll() {
  BOOL success;
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* overlapped;
  ol_req* req;
  ol_handle* handle;
  ol_handle *peer;

  success = GetQueuedCompletionStatus(ol_iocp_,
                                      &bytes,
                                      &key,
                                      &overlapped,
                                      INFINITE);

  if (!success && !overlapped)
    ol_fatal_error(GetLastError(), "GetQueuedCompletionStatus");

  req = ol_overlapped_to_req(overlapped);

  switch (req->type) {
    case OL_ACCEPT:
      peer = (ol_handle*)req->data;

      /* If the listening socket got closed in the meantime, disregard this */
      /* result. However we still need to free the peer socket allocated */
      /* by ol_queue_accept. */
      if (req->_.flags & OL_REQ_STRAY) {
        ol_close(peer);
        ol_free(peer);
        free(req);
        return;
      }

      handle = (ol_handle*)key;

      success = GetOverlappedResult(handle->_.handle, overlapped, &bytes, FALSE);

      if (success && handle->accept_cb) {
        handle->accept_cb(handle, peer);

      } else {
        ol_close(peer);
        ol_free(peer);
        
        if (!success) {
          ol_errno_ = GetLastError();
          /* Todo: actually handle the error instead of dying */
          ol_fatal_error(GetLastError(), "AcceptEx");
        }
      }

      /* Queue another accept */
      ol_queue_accept(handle);
      return;
  }
}


int ol_run() {
  for (;;) {
    ol_poll();
  }
  return 0;
}
