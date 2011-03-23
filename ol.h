#ifdef windows
# include "ol_win.h"
#else
# include "ol_unix.h"
#endif


typedef ol_read_cb void(*)(ol_buf *bufs, int bufcnt);
typedef ol_close_cb void(*)(int read, int write);
typedef ol_connect_cb void(*)();
typedef ol_connect_cb void(*)();


typedef enum {
  OL_NAMED_PIPE,
  OL_TCP,
  OL_TCP6
} ol_socket_type;


typedef struct {
  size_t len;
  char* buf;
} ol_buf;


typedef struct {
  size_t len;
  void* name;
} ol_addr;


/**
 * Creates a new socket of given type. If bind_addr is NULL a random
 * port will be bound in the case of OL_TCP and OL_TCP6. In the case
 * of NAMED_PIPE, bind_addr specifies a string describing the location
 * to bind to.
 */
ol_socket* ol_socket_create(ol_socket_type type, ol_buf* bind_addr,
    ol_read_cb cb, ol_close_cb cb);


int ol_socket_connect(ol_socket* socket, ol_addr addr,
    ol_buf* buf, size_t* bytes_sent, ol_connect_cb ol);


int ol_socket_pause(ol_socket* socket);


int ol_socket_resume(ol_socket* socket);


int ol_socket_address(ol_socket* socket, ol_addr* addr);


/**
 * Send data to socket. User responsible for bufs until callback is made.
 * Multiple ol_socket_write() calls may be issued before the previous ones
 * complete - data will sent in the correct order.
 */
int ol_socket_write(ol_socket* socket, ol_buf* bufs, int bufcnt,
    size_t* bytes_sent, ol_write_cb cb);


int ol_socket_listen(ol_socket* server, int backlog, ol_accept_cb cb);


int ol_socket_shutdown_write(ol_socket* socket);


int ol_socket_close(ol_socket* socket);


int ol_socket_free(ol_socket* socket);
