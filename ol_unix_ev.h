

/**
 * Note can be cast to io_vec.
 */
typedef struct _ol_buf {
  char* buf;
  size_t len;
  ngx_queue_s write_queue;
} ol_buf;



typedef struct _ol_handle {
  int fd;

  ol_read_cb read_cb;
  ol_close_cb close_cb;

  ngx_queue_s write_queue;
  ngx_queue_s all_handles;
} ol_handle;
