
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>


/**
 * It should be possible to cast oio_buf[] to WSABUF[]
 * see http://msdn.microsoft.com/en-us/library/ms741542(v=vs.85).aspx
 */
typedef struct oio_buf {
  ULONG len;
  char* base;
} oio_buf;

struct oio_req_s {
  struct oio_req_shared_s;
  OVERLAPPED overlapped;
  int flags;
};

typedef struct {
  oio_req req;
  SOCKET socket;

  /* AcceptEx specifies that the buffer must be big enough to at least hold */
  /* two socket addresses plus 32 bytes. */
  char buffer[sizeof(struct sockaddr_storage) * 2 + 32];
} oio_accept_data;

struct oio_handle_s {
  struct oio_handle_shared_s;
  union {
    SOCKET socket;
    HANDLE handle;
  };
  oio_accept_data *accept_data;
  unsigned int flags;
  unsigned int reqs_pending;
  oio_err error;
};
