
#include <stdint.h>
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "tree.h"


/**
 * It should be possible to cast oio_buf[] to WSABUF[]
 * see http://msdn.microsoft.com/en-us/library/ms741542(v=vs.85).aspx
 */
typedef struct oio_buf {
  ULONG len;
  char* base;
} oio_buf;

struct oio_req_private_s {
  union {
    /* Used by I/O operations */
    OVERLAPPED overlapped;
    /* Used by timers */
    struct {
      RB_ENTRY(oio_req_s) tree_entry;
      int64_t due;
    };
  };
  int flags;
};

struct oio_handle_private_s {
  union {
    SOCKET socket;
    HANDLE handle;
  };
  SOCKET accepted_socket;
  struct oio_accept_req_s* accept_reqs;
  unsigned int flags;
  unsigned int reqs_pending;
  oio_err error;
};
