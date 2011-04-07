
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>


/**
 * It should be possible to cast ol_buf[] to WSABUF[]
 * see http://msdn.microsoft.com/en-us/library/ms741542(v=vs.85).aspx
 */
typedef struct _ol_buf {
  ULONG len;
  char* base;
} ol_buf;

struct ol_req_s {
  struct ol_req_shared_s;
  OVERLAPPED overlapped;
  int flags;
};

typedef struct {
  ol_req req;
  SOCKET socket;

  /* AcceptEx specifies that the buffer must be big enough to at least hold */
  /* two socket addresses plus 32 bytes. */
  char buffer[sizeof(struct sockaddr_storage) * 2 + 32];
} ol_accept_data;

struct ol_handle_s {
  struct ol_handle_shared_s;
  union {
    SOCKET socket;
    HANDLE handle;
  };
  ol_accept_data *accept_data;
  unsigned int flags;
  unsigned int reqs_pending;
  ol_err error;
};
