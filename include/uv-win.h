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
# define _WIN32_WINNT   0x0600
#endif

#if !defined(_SSIZE_T_) && !defined(_SSIZE_T_DEFINED)
typedef intptr_t ssize_t;
# define _SSIZE_T_
# define _SSIZE_T_DEFINED
#endif

#include <winsock2.h>

#ifndef LOCALE_INVARIANT
# define LOCALE_INVARIANT 0x007f
#endif

#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <process.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdint.h>

#include "tree.h"
#include "uv-threadpool.h"

#define MAX_PIPENAME_LEN 256

#ifndef S_IFLNK
# define S_IFLNK 0xA000
#endif

/* Additional signals supported by uv_signal and or uv_kill. The CRT defines
 * the following signals already:
 *
 *   #define SIGINT           2
 *   #define SIGILL           4
 *   #define SIGABRT_COMPAT   6
 *   #define SIGFPE           8
 *   #define SIGSEGV         11
 *   #define SIGTERM         15
 *   #define SIGBREAK        21
 *   #define SIGABRT         22
 *
 * The additional signals have values that are common on other Unix
 * variants (Linux and Darwin)
 */
#define SIGHUP                1
#define SIGKILL               9
#define SIGWINCH             28

/* The CRT defines SIGABRT_COMPAT as 6, which equals SIGABRT on many */
/* unix-like platforms. However MinGW doesn't define it, so we do. */
#ifndef SIGABRT_COMPAT
# define SIGABRT_COMPAT       6
#endif


typedef int (WSAAPI* LPFN_WSARECV)
            (SOCKET socket,
             LPWSABUF buffers,
             DWORD buffer_count,
             LPDWORD bytes,
             LPDWORD flags,
             LPWSAOVERLAPPED overlapped,
             LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine);

typedef int (WSAAPI* LPFN_WSARECVFROM)
            (SOCKET socket,
             LPWSABUF buffers,
             DWORD buffer_count,
             LPDWORD bytes,
             LPDWORD flags,
             struct sockaddr* addr,
             LPINT addr_len,
             LPWSAOVERLAPPED overlapped,
             LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine);

#ifndef _NTDEF_
  typedef LONG NTSTATUS;
  typedef NTSTATUS *PNTSTATUS;
#endif

#ifndef RTL_CONDITION_VARIABLE_INIT
  typedef PVOID CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#endif

typedef struct _AFD_POLL_HANDLE_INFO {
  HANDLE Handle;
  ULONG Events;
  NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
  LARGE_INTEGER Timeout;
  ULONG NumberOfHandles;
  ULONG Exclusive;
  AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

#define UV_MSAFD_PROVIDER_COUNT 3


/**
 * It should be possible to cast uv_buf_t[] to WSABUF[]
 * see http://msdn.microsoft.com/en-us/library/ms741542(v=vs.85).aspx
 */
typedef struct uv_buf_t {
  ULONG len;
  char* base;
} uv_buf_t;

typedef int uv_file;
typedef SOCKET uv_os_sock_t;
typedef HANDLE uv_os_fd_t;

typedef HANDLE uv_thread_t;

typedef HANDLE uv_sem_t;

typedef CRITICAL_SECTION uv_mutex_t;

typedef CONDITION_VARIABLE uv_cond_t;

typedef struct {
  unsigned int num_readers_;
  CRITICAL_SECTION num_readers_lock_;
  HANDLE write_semaphore_;
} uv_rwlock_t;

typedef struct {
  unsigned int n;
  unsigned int count;
  uv_mutex_t mutex;
  uv_sem_t turnstile1;
  uv_sem_t turnstile2;
} uv_barrier_t;

typedef struct {
  DWORD tls_index;
} uv_key_t;

#define UV_ONCE_INIT { 0, NULL }

typedef struct uv_once_s {
  unsigned char ran;
  HANDLE event;
} uv_once_t;

/* Platform-specific definitions for uv_spawn support. */
typedef unsigned char uv_uid_t;
typedef unsigned char uv_gid_t;

typedef struct uv__dirent_s {
  int d_type;
  char d_name[1];
} uv__dirent_t;

#define HAVE_DIRENT_TYPES
#define UV__DT_DIR     UV_DIRENT_DIR
#define UV__DT_FILE    UV_DIRENT_FILE
#define UV__DT_LINK    UV_DIRENT_LINK
#define UV__DT_FIFO    UV_DIRENT_FIFO
#define UV__DT_SOCKET  UV_DIRENT_SOCKET
#define UV__DT_CHAR    UV_DIRENT_CHAR
#define UV__DT_BLOCK   UV_DIRENT_BLOCK

/* Platform-specific definitions for uv_dlopen support. */
#define UV_DYNAMIC FAR WINAPI
typedef struct {
  HMODULE handle;
  char* errmsg;
} uv_lib_t;

#define UV_LOOP_PRIVATE_FIELDS                                                \
    /* The loop's I/O completion port */                                      \
  HANDLE iocp;                                                                \
  /* Tail of a single-linked circular queue of pending reqs. If the queue */  \
  /* is empty, tail_ is NULL. If there is only one item, */                   \
  /* tail_->next_req == tail_ */                                              \
  uv_req_t* pending_reqs_tail;                                                \
  /* Head of a single-linked list of closed handles */                        \
  uv_handle_t* endgame_handles;                                               \
  /* This handle holds the peer sockets for the fast variant of uv_poll_t */  \
  SOCKET poll_peer_sockets[UV_MSAFD_PROVIDER_COUNT];                          \
  /* Async handle */                                                          \
  struct uv_req_s async_req;                                                  \
  /* Global queue of loops */                                                 \
  void* loops_queue[2];

#define UV_REQ_TYPE_PRIVATE                                                   \
  /* TODO: remove the req suffix */                                           \
  UV_ACCEPT,                                                                  \
  UV_FS_EVENT_REQ,                                                            \
  UV_POLL_REQ,                                                                \
  UV_PROCESS_EXIT,                                                            \
  UV_READ,                                                                    \
  UV_UDP_RECV,                                                                \
  UV_WAKEUP,                                                                  \
  UV_SIGNAL_REQ,

#define UV_REQ_PRIVATE_FIELDS                                                 \
  union {                                                                     \
    /* Used by I/O operations */                                              \
    struct {                                                                  \
      OVERLAPPED overlapped;                                                  \
      size_t queued_bytes;                                                    \
    } io;                                                                     \
  } u;                                                                        \
  struct uv_req_s* next_req;

#define UV_WRITE_PRIVATE_FIELDS                                               \
  int ipc_header;                                                             \
  uv_buf_t write_buffer;                                                      \
  HANDLE event_handle;                                                        \
  HANDLE wait_handle;

#define UV_CONNECT_PRIVATE_FIELDS                                             \
  /* empty */

#define UV_SHUTDOWN_PRIVATE_FIELDS                                            \
  /* empty */

#define UV_UDP_SEND_PRIVATE_FIELDS                                            \
  /* empty */

#define UV_PRIVATE_REQ_TYPES                                                  \
  typedef struct uv_pipe_accept_s {                                           \
    UV_REQ_FIELDS                                                             \
    HANDLE pipeHandle;                                                        \
    struct uv_pipe_accept_s* next_pending;                                    \
  } uv_pipe_accept_t;                                                         \
                                                                              \
  typedef struct uv_tcp_accept_s {                                            \
    UV_REQ_FIELDS                                                             \
    SOCKET accept_socket;                                                     \
    char accept_buffer[sizeof(struct sockaddr_storage) * 2 + 32];             \
    HANDLE event_handle;                                                      \
    HANDLE wait_handle;                                                       \
    struct uv_tcp_accept_s* next_pending;                                     \
  } uv_tcp_accept_t;                                                          \
                                                                              \
  typedef struct uv_read_s {                                                  \
    UV_REQ_FIELDS                                                             \
    HANDLE event_handle;                                                      \
    HANDLE wait_handle;                                                       \
  } uv_read_t;

#define uv_stream_connection_fields                                           \
  unsigned int write_reqs_pending;                                            \
  uv_shutdown_t* shutdown_req;

#define uv_stream_server_fields                                               \
  uv_connection_cb connection_cb;

#define UV_STREAM_PRIVATE_FIELDS                                              \
  unsigned int reqs_pending;                                                  \
  int activecnt;                                                              \
  uv_read_t read_req;                                                         \
  union {                                                                     \
    struct { uv_stream_connection_fields } conn;                              \
    struct { uv_stream_server_fields     } serv;                              \
  } stream;

#define uv_tcp_server_fields                                                  \
  uv_tcp_accept_t* accept_reqs;                                               \
  unsigned int processed_accepts;                                             \
  uv_tcp_accept_t* pending_accepts;                                           \
  LPFN_ACCEPTEX func_acceptex;

#define uv_tcp_connection_fields                                              \
  uv_buf_t read_buffer;                                                       \
  LPFN_CONNECTEX func_connectex;

#define UV_TCP_PRIVATE_FIELDS                                                 \
  SOCKET socket;                                                              \
  int delayed_error;                                                          \
  union {                                                                     \
    struct { uv_tcp_server_fields } serv;                                     \
    struct { uv_tcp_connection_fields } conn;                                 \
  } tcp;

#define UV_UDP_PRIVATE_FIELDS                                                 \
  SOCKET socket;                                                              \
  unsigned int reqs_pending;                                                  \
  int activecnt;                                                              \
  uv_req_t recv_req;                                                          \
  uv_buf_t recv_buffer;                                                       \
  struct sockaddr_storage recv_from;                                          \
  int recv_from_len;                                                          \
  uv_udp_recv_cb recv_cb;                                                     \
  uv_alloc_cb alloc_cb;                                                       \
  LPFN_WSARECV func_wsarecv;                                                  \
  LPFN_WSARECVFROM func_wsarecvfrom;

#define uv_pipe_server_fields                                                 \
  int pending_instances;                                                      \
  uv_pipe_accept_t* accept_reqs;                                              \
  uv_pipe_accept_t* pending_accepts;

#define uv_pipe_connection_fields                                             \
  uv_timer_t* eof_timer;                                                      \
  uv_write_t ipc_header_write_req;                                            \
  int ipc_pid;                                                                \
  uint64_t remaining_ipc_rawdata_bytes;                                       \
  struct {                                                                    \
    void* queue[2];                                                           \
    int queue_len;                                                            \
  } pending_ipc_info;                                                         \
  uv_write_t* non_overlapped_writes_tail;                                     \
  uv_mutex_t readfile_mutex;                                                  \
  volatile HANDLE readfile_thread;

#define UV_PIPE_PRIVATE_FIELDS                                                \
  HANDLE handle;                                                              \
  WCHAR* name;                                                                \
  union {                                                                     \
    struct { uv_pipe_server_fields } serv;                                    \
    struct { uv_pipe_connection_fields } conn;                                \
  } pipe;

/* TODO: put the parser states in an union - TTY handles are always */
/* half-duplex so read-state can safely overlap write-state. */
#define UV_TTY_PRIVATE_FIELDS                                                 \
  HANDLE handle;                                                              \
  union {                                                                     \
    struct {                                                                  \
      /* Used for readable TTY handles */                                     \
      uv_buf_t read_line_buffer;                                              \
      HANDLE read_raw_wait;                                                   \
      /* Fields used for translating win keystrokes into vt100 characters */  \
      char last_key[8];                                                       \
      unsigned char last_key_offset;                                          \
      unsigned char last_key_len;                                             \
      WCHAR last_utf16_high_surrogate;                                        \
      INPUT_RECORD last_input_record;                                         \
    } rd;                                                                     \
    struct {                                                                  \
      /* Used for writable TTY handles */                                     \
      /* utf8-to-utf16 conversion state */                                    \
      unsigned int utf8_codepoint;                                            \
      unsigned char utf8_bytes_left;                                          \
      /* eol conversion state */                                              \
      unsigned char previous_eol;                                             \
      /* ansi parser state */                                                 \
      unsigned char ansi_parser_state;                                        \
      unsigned char ansi_csi_argc;                                            \
      unsigned short ansi_csi_argv[4];                                        \
      COORD saved_position;                                                   \
      WORD saved_attributes;                                                  \
    } wr;                                                                     \
  } tty;

#define UV_POLL_PRIVATE_FIELDS                                                \
  SOCKET socket;                                                              \
  /* Used in fast mode */                                                     \
  SOCKET peer_socket;                                                         \
  AFD_POLL_INFO afd_poll_info_1;                                              \
  AFD_POLL_INFO afd_poll_info_2;                                              \
  /* Used in fast and slow mode. */                                           \
  uv_req_t poll_req_1;                                                        \
  uv_req_t poll_req_2;                                                        \
  unsigned char submitted_events_1;                                           \
  unsigned char submitted_events_2;                                           \
  unsigned char mask_events_1;                                                \
  unsigned char mask_events_2;                                                \
  unsigned char events;

#define UV_ASYNC_PRIVATE_FIELDS                                               \
  LONG volatile async_sent;

#define UV_HANDLE_PRIVATE_FIELDS                                              \
  uv_handle_t* endgame_next;                                                  \

#define UV_GETADDRINFO_PRIVATE_FIELDS                                         \
  struct uv__work work_req;                                                   \
  uv_getaddrinfo_cb getaddrinfo_cb;                                           \
  void* alloc;                                                                \
  WCHAR* node;                                                                \
  WCHAR* service;                                                             \
  /* The addrinfoW field is used to store a pointer to the hints, and    */   \
  /* later on to store the result of GetAddrInfoW. The final result will */   \
  /* be converted to struct addrinfo* and stored in the addrinfo field.  */   \
  struct addrinfoW* addrinfow;                                                \
  struct addrinfo* addrinfo;                                                  \
  int retcode;

#define UV_PROCESS_PRIVATE_FIELDS                                             \
  struct uv_process_exit_s {                                                  \
    UV_REQ_FIELDS                                                             \
  } exit_req;                                                                 \
  BYTE* child_stdio_buffer;                                                   \
  int exit_signal;                                                            \
  HANDLE wait_handle;                                                         \
  HANDLE process_handle;                                                      \
  volatile char exit_cb_pending;

#define UV_FS_PRIVATE_FIELDS                                                  \
  struct uv__work work_req;                                                   \
  int flags;                                                                  \
  DWORD sys_errno_;                                                           \
  union {                                                                     \
    /* TODO: remove me in 0.9. */                                             \
    WCHAR* pathw;                                                             \
    int fd;                                                                   \
  } file;                                                                     \
  union {                                                                     \
    struct {                                                                  \
      int mode;                                                               \
      WCHAR* new_pathw;                                                       \
      int file_flags;                                                         \
      int fd_out;                                                             \
      unsigned int nbufs;                                                     \
      uv_buf_t* bufs;                                                         \
      int64_t offset;                                                         \
      uv_buf_t bufsml[4];                                                     \
    } info;                                                                   \
    struct {                                                                  \
      double atime;                                                           \
      double mtime;                                                           \
    } time;                                                                   \
  } fs;

#define UV_FS_EVENT_PRIVATE_FIELDS                                            \
  struct uv_fs_event_req_s {                                                  \
    UV_REQ_FIELDS                                                             \
  } req;                                                                      \
  HANDLE dir_handle;                                                          \
  int req_pending;                                                            \
  uv_fs_event_cb cb;                                                          \
  WCHAR* filew;                                                               \
  WCHAR* short_filew;                                                         \
  WCHAR* dirw;                                                                \
  char* buffer;

#define UV_SIGNAL_PRIVATE_FIELDS                                              \
  RB_ENTRY(uv_signal_s) tree_entry;                                           \
  struct uv_req_s signal_req;                                                 \
  unsigned long pending_signum;

#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif
