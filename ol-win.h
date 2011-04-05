
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


typedef struct {
  OVERLAPPED overlapped;
  int flags;
} ol_req_private;

typedef struct {
  union {
    SOCKET socket;
    HANDLE handle;
  };
  unsigned int flags;
  unsigned int reqs_pending;
  ol_err error;
} ol_handle_private;