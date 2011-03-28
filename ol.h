typedef int ol_err; // FIXME

typedef void (*)(ol_req* req, ol_err e) ol_req_cb;
typedef void (*)(ol_req* req, size_t nread, ol_err e) ol_read_cb;
typedef void (*)(ol_req* req, ol_err e) ol_write_cb;
typedef void (*)(ol_handle* server, ol_handle* new_client) ol_accept_cb;
typedef void (*)(ol_handle* handle, ol_err e) ol_close_cb;


typedef enum {
  OL_UNKNOWN_HANDLE = 0,
  OL_TCP_CONNECTION,
  OL_TCP_SERVER,
  OL_NAMED_PIPE,
  OL_TTY,
  OL_FILE,
} ol_handle_type;


typesef struct {
  // read-only
  ol_handle_type type;
  // private
  ol_handle_private _;
  // public
  ol_accept_cb accept_cb;
  ol_close_cb close_cb;
  void* data;
} ol_handle;


typedef enum {
  OL_UNKNOWN_REQ = 0,
  OL_CONNECT,
  OL_READ,
  OL_WRITE
  OL_SHUTDOWN
} ol_req_type;


typedef struct {
  // read-only
  ol_req_type type;
  ol_handle *handle;
  // private
  ol_req_private _;
  // public
  union {
    ol_read_cb read_cb;
    ol_write_cb write_cb;
    ol_req_cb connect_cb;
    ol_req_cb shutdown_cb;
  };
  void *data;
} ol_req;


int ol_run();

ol_handle* ol_handle_new(ol_close_cb close_cb, void* data);

// TCP server methods.
int ol_bind(ol_handle* handle, sockaddr* addr);
int ol_listen(ol_handle* handle, int backlog, ol_accept_cb cb);

// TCP socket methods.
int ol_connect(ol_handle* handle, ol_req *req, sockaddr* addr);
int ol_read(ol_handle* handle, ol_req *req, ol_buf* bufs, int bufcnt);
int ol_write(ol_handle* handle, ol_req *req, ol_buf* bufs, int bufcnt);
int ol_shutdown(ol_handle* handle, ol_req *req);

// Request handle to be closed. close_cb will be made
// synchronously during this call.
int ol_close(ol_handle* handle);

// Must be called for all handles after close_cb. Handles that arrive
// via the accept_cb must use ol_free().
int ol_free(ol_handle* handle);
