/**
 * Note can be cast to io_vec.
 */
typedef struct {
  char* buf;
  size_t len;
  ngx_queue_s write_queue;
} ol_buf;



typedef struct {
  int fd;
  ol_handle_type type;

  ol_read_cb read_cb;
  ol_close_cb close_cb;
  ol_connect_cb connect_cb;

  ev_io read_watcher;
  ev_io write_watcher;

  ngx_queue_s write_queue;
  ngx_queue_s all_handles;
} ol_handle;
