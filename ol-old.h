/**
 * Overlapped I/O for every operating system.
 */

#ifdef __POSIX__
# include "ol-unix.h"
#else
# include "ol-win.h"
#endif

typedef struct {
  int code;
  const char* msg;
} ol_err;

/**
 * Error codes are not cross-platform, so we have our own.
 */
typedef enum {
  OL_SUCCESS = 0,
  OL_EPENDING = -1,
  OL_EPIPE = -2,
  OL_EMEM = -3
} ol_err;


inline const char* ol_err_string(int errorno) {
  switch (errorno) {
    case OL_SUCCESS:
    case OL_EPENDING:
      return "";

    case OL_EPIPE:
      return "EPIPE: Write to non-writable handle";

    case OL_EMEM:
      return "EMEM: Out of memory!";

    default:
      assert(0);
      return "Unknown error code. Bug.";
  }
}


/**
 * Do not make assumptions about the order of the elements in this sturct.
 * Always use offsetof because the order is platform dependent. Has a char*
 * buf and size_t len. That's all you need to know.
 */
struct ol_buf;


typedef enum {
  OL_TCP,
  OL_TCP6,
  OL_NAMED_PIPE,
  OL_FILE,
  OL_TTY
} ol_handle_type;


typedef void(*)(ol_handle* h, ol_buf *bufs, int bufcnt) ol_read_cb;
typedef void(*)(ol_handle* h) ol_connect_cb;
typedef void(*)(ol_handle* h, ol_handle *peer) ol_accept_cb;
typedef void(*)(ol_handle* h) ol_write_cb;


typedef enum {
  OL_READ,
  OL_WRITE,
  OL_CONNECT,
  OL_ACCEPT,
  OL_DESTROY
} ol_req_type;


typedef struct {
  ol_req_type type;
  ol_req_private _;
  /* following are rw */
  union {
    ol_write_cb write_cb;
    ol_connect_cb connect_cb;
  };
  void* data; /* rw */
} ol_req;


ol_handle* ol_handle_new();


ol_handle* ol_open_file(ol_handle* h, ol_req* req, char *filename);
ol_handle* ol_open_named_pipe(ol_handle* h, ol_req* req, char *filename);
ol_handle* ol_open_tty(ol_handle* h, ol_req* req);


struct sockaddr oi_ip4_addr(char*, int port);

/**
 * Depth of write buffer in bytes.
 */
size_t ol_buffer_size(ol_handle* h);


/**
 * Returns file descriptor associated with the handle. There may be only
 * limited numbers of file descriptors allowed by the operating system. On
 * Windows this limit is 2048 (see
 * _setmaxstdio[http://msdn.microsoft.com/en-us/library/6e3b887c.aspx])
 */
int ol_get_fd(ol_handle* h);


/**
 * Returns the type of the handle.
 */
ol_handle_type ol_get_type(ol_handle* h);


/**
 * Only works with named pipes and TCP sockets.
 */
int ol_connect(ol_handle* h, ol_req* req, sockaddr* addr, ol_buf* initial_buf);


int ol_accept(ol_handle* h, ol_req* req);


/**
 * Send data to handle. User responsible for bufs until callback is made.
 * Multiple ol_handle_write() calls may be issued before the previous ones
 * complete - data will sent in the correct order.
 *
 * Returns zero on succuessful write and bytes_sent is filled with the
 * number of bytes successfully written. If an asyncrhonous write was
 * successfully initiated then OL_EAGAIN is returned.
 */
int ol_write(ol_handle* h, ol_req* req, ol_buf* bufs, int bufcnt);
int ol_write2(ol_handle* h, ol_req* req, const char *string);


int ol_read(ol_handle* h, ol_req* req, ol_buf* bufs, int bufcnt);


/**
 * Works on both named pipes and TCP handles. Synchronous.
 */
int ol_listen(ol_handle* h, int backlog);


/**
 * See http://msdn.microsoft.com/en-us/library/ms737757(v=VS.85).aspx
 */
int ol_disconnect(ol_handle* h, ol_req* req);


/**
 * Immediately closes the handle. If there is data in the send buffer
 * it will not be sent.
 */
int ol_close(ol_handle* h);


/**
 * Releases memory associated with handle. You MUST call this after
 * is made with both 0 arguments.
 */
int ol_free(ol_handle* h);




ol_loop* ol_loop_new();


void ol_associate(ol_loop* loop, ol_handle* handle);


void ol_loop_free(ol_loop* loop);


void ol_run(ol_loop* loop);




