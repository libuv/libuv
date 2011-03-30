
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


OVERLAPPED* ol_req_to_overlapped(ol_req* req) {
  return &(req->_.overlapped);
}


ol_req* ol_overlapped_to_req(OVERLAPPED* overlapped) {
  return CONTAINING_RECORD(overlapped, ol_req, _.overlapped);
}


void ol_poll() {
  BOOL success;
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED *overlapped;
  ol_req *req;

  success = GetQueuedCompletionStatus(ol_iocp_,
                                      &bytes,
                                      &key,
                                      &overlapped,
                                      INFINITE);

  if (!success && !overlapped)
    ol_fatal_error(GetLastError(), "GetQueuedCompletionStatus");

  req = ol_overlapped_to_req(overlapped);

  switch (req->type) {

  }
}


int ol_run() {
  for (;;) {
    ol_poll();
  }
  return 0;
}

ol_handle* ol_tcp_handle_new(ol_close_cb close_cb, void* data) {
  ol_handle *handle;
  int yes = 1;

  handle = (ol_handle*)calloc(sizeof(ol_handle), 1);
  handle->close_cb = close_cb;
  handle->data = data;
  handle->type = OL_TCP;

  /* Lazily allocate a file descriptor for this handle */
  handle->_.socket = socket(AF_INET, SOCK_STREAM, 0);
  if (handle->_.socket == INVALID_SOCKET) {
    ol_errno_ = WSAGetLastError();
    free(handle);
    return NULL;
  }

  handle->type = OL_TCP;
  
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
    closesocket(handle->_.socket);
    free(handle);
    return NULL;
  }

  /* Associate it with the I/O completion port. */
  /* Use ol_handle pointer as completion key. */
  if (CreateIoCompletionPort(handle->_.handle, 
                             ol_iocp_, 
                             (ULONG_PTR)handle, 
                             0) == NULL) {
    ol_errno_ = GetLastError();
    closesocket(handle->_.socket);
    free(handle);
    return NULL;
  }
  
  return handle;
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


int ol_close(ol_handle* handle) {
  switch (handle->type) {
    case OL_TCP:
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

