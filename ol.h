/**
 * Overlapped I/O for every operating system.
 */

#ifdef __POSIX__
# include "ol_unix.h"
#else
# include "ol_win.h"
#endif


/**
 * Do not make assumptions about the order of the elements in this sturct.
 * Always use offsetof because the order is platform dependent. Has a char*
 * buf and size_t len. That's all you need to know.
 */
struct ol_buf;


typedef ol_read_cb void(*)(ol_buf *bufs, int bufcnt);
typedef ol_close_cb void(*)(int read, int write);
typedef ol_connect_cb void(*)();
typedef ol_accept_cb void(*)(ol_handle *peer);


/**
 * Creates a tcp handle used for both client and servers.
 */
ol_handle* ol_tcp_new(int v4, ol_read_cb read_cb, ol_close_cb close_cb);


/**
 * Creates a new file handle. The 'read' parameter is boolean indicating if
 * the file should be read from or created.
 */
ol_handle* ol_file_new(char *filename, int read, ol_read_cb cb,
    ol_close_cb cb);


/**
 * In the case of servers, give a filename. In the case of clients
 * leave filename NULL.
 */
ol_handle* ol_named_pipe_new(char *filename, ol_read_cb cb,
    ol_close_cb cb);


/**
 * Allocates a new tty handle.
 */
ol_handle* ol_tty_new(ol_tty_read_cb cb, ol_close_cb cb);


/**
 * Only works with named pipes and TCP sockets.
 */
int ol_connect(ol_handle* h, sockaddr* addr, sockaddr_len len,
    ol_buf* buf, size_t* bytes_sent, ol_connect_cb cb);


/**
 * Only works for TCP sockets.
 */
int ol_bind(ol_handle* h, sockaddr* addr, sockaddr_len len);


/**
 * Depth of write buffer in bytes.
 */
size_t ol_buffer_size(ol_handle* h);


int ol_pause(ol_handle* h);


int ol_resume(ol_handle* h);


/**
 * Returns file descriptor associated with the handle. There may be only
 * limited numbers of file descriptors allowed by the operating system. On
 * Windows this limit is 2048 (see
 * _setmaxstdio[http://msdn.microsoft.com/en-us/library/6e3b887c.aspx])
 */
int ol_get_fd(ol_handle* h);


/**
 * Send data to h. User responsible for bufs until callback is made.
 * Multiple ol_handle_write() calls may be issued before the previous ones
 * complete - data will sent in the correct order.
 */
int ol_write(ol_handle* h, ol_buf* bufs, int bufcnt,
    size_t* bytes_sent, ol_write_cb cb);


/**
 * Note: works on both named pipes and TCP handles.
 */
int ol_listen(ol_handle* h, int backlog, ol_accept_cb cb);


/**
 * Writes EOF or sends a FIN packet.
 */
int ol_end(ol_handle* h);


int ol_close(ol_handle* h);


/**
 * Releases memory associated with handle. You MUST call this after
 * ol_close_cb() is made with both 0 arguments.
 */
int ol_free(ol_handle* h);




ol_loop* ol_loop_new();


ol_loop* ol_associate(ol_handle* handle);


void ol_loop_free(ol_loop* loop);


void ol_run(ol_loop* loop);




