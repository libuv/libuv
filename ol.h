/**
 * Overlapped I/O for every operating system.
 */

#ifdef __POSIX__
# include "ol_unix.h"
#else
# include "ol_win.h"
#endif

/**
 * Error codes are not cross-platform, so we have our own.
 */
typedef enum {
  OL_SUCCESS = 0,
  OL_EPENDING = -1, /* Windows users think WSA_IO_PENDING */
  OL_EPIPE = -2,
  OL_EMEM = -3,
} ol_errno;


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
typedef void(*)(ol_handle* h, int read, int write, ol_errno err) ol_close_cb;
typedef void(*)(ol_handle* h) ol_connect_cb;
typedef void(*)(ol_handle* h, ol_handle *peer) ol_accept_cb;


/**
 * Creates a tcp handle used for both client and servers.
 */
ol_handle* ol_tcp_new(int v4, ol_read_cb read_cb, ol_close_cb close_cb);


/**
 * Creates a new file handle. The 'read' parameter is boolean indicating if
 * the file should be read from or created.
 */
ol_handle* ol_file_new(char *filename, int read, ol_read_cb read_cb,
    ol_close_cb close_cb);


/**
 * In the case of servers, give a filename. In the case of clients
 * leave filename NULL.
 */
ol_handle* ol_named_pipe_new(char *filename, ol_read_cb read_cb,
    ol_close_cb close_cb);


/**
 * Allocates a new tty handle.
 */
ol_handle* ol_tty_new(ol_tty_read_cb read_cb, ol_close_cb close_cb);


/**
 * Only works with named pipes and TCP sockets.
 */
int ol_connect(ol_handle* h, sockaddr* addr, sockaddr_len len,
    ol_buf* buf, ol_connect_cb connect_cb);


/**
 * Only works for TCP sockets.
 */
int ol_bind(ol_handle* h, sockaddr* addr, sockaddr_len len);


/**
 * Depth of write buffer in bytes.
 */
size_t ol_buffer_size(ol_handle* h);


int ol_read_stop(ol_handle* h);


int ol_read_start(ol_handle* h);


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
 * Send data to handle. User responsible for bufs until callback is made.
 * Multiple ol_handle_write() calls may be issued before the previous ones
 * complete - data will sent in the correct order.
 *
 * Returns zero on succuessful write and bytes_sent is filled with the
 * number of bytes successfully written. If an asyncrhonous write was
 * successfully initiated then OL_EAGAIN is returned.
 */
int ol_write(ol_handle* h, ol_buf* bufs, int bufcnt, ol_write_cb cb);


/**
 * Works on both named pipes and TCP handles.
 */
int ol_listen(ol_handle* h, int backlog, ol_accept_cb cb);


/**
 * Writes EOF or sends a FIN packet.
 * Further calls to ol_write() result in OI_EPIPE error. When the send
 * buffer is drained and the other side also terminates their writes, the
 * handle is finally closed and ol_close_cb() made. There is no need to call
 * ol_close() after this.
 */
int ol_graceful_close(ol_handle* h);


/**
 * Immediately closes the handle. If there is data in the send buffer
 * it will not be sent.
 */
int ol_close(ol_handle* h);


/**
 * Releases memory associated with handle. You MUST call this after
 * ol_close_cb() is made with both 0 arguments.
 */
int ol_free(ol_handle* h);




ol_loop* ol_loop_new();


void ol_associate(ol_loop* loop, ol_handle* handle);


void ol_loop_free(ol_loop* loop);


void ol_run(ol_loop* loop);




