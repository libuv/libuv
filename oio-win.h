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

#ifndef _WIN32_WINNT
# define _WIN32_WINNT   0x0501
#endif

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

#define oio_req_private_fields            \
  union {                                 \
    /* Used by I/O operations */          \
    OVERLAPPED overlapped;                \
    /* Used by timers */                  \
    struct {                              \
      RB_ENTRY(oio_req_s) tree_entry;     \
      int64_t due;                        \
    };                                    \
  };                                      \
  int flags;

#define oio_tcp_connection_fields         \
  void* read_cb;                          \
  struct oio_req_s read_req;              \
  unsigned int write_reqs_pending;        \
  oio_req_t* shutdown_req;

#define oio_tcp_server_fields             \
  void *accept_cb;                        \
  SOCKET accept_socket;                   \
  struct oio_req_s accept_req;            \
  char accept_buffer[sizeof(struct sockaddr_storage) * 2 + 32];

#define oio_tcp_fields                    \
  unsigned int reqs_pending;              \
  union {                                 \
    SOCKET socket;                        \
    HANDLE handle;                        \
  };                                      \
  union {                                 \
    struct { oio_tcp_connection_fields }; \
    struct { oio_tcp_server_fields     }; \
  };

#define oio_loop_fields                   \
  oio_handle_t* loop_prev;                  \
  oio_handle_t* loop_next;                  \
  void* loop_cb;

#define oio_handle_private_fields         \
  oio_handle_t* endgame_next;               \
  unsigned int flags;                     \
  oio_err error;                          \
  union {                                 \
    struct { oio_tcp_fields  };           \
    struct { oio_loop_fields };           \
  };
