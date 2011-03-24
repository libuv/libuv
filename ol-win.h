

/**
 * Note can be cast to
 * WSABUF[http://msdn.microsoft.com/en-us/library/ms741542(v=vs.85).aspx]
 */
typedef struct _ol_buf {
  u_long len;
  char* buf;
  _ol_buf* next;
  _ol_buf* prev;
} ol_buf;
