#include "internal.h"
#include "liburing.h"
#include "uv.h"

#include <errno.h>


/* Return values:
 * 0 on success.
 * UV_ENOSYS if io_uring is not available.
 * UV_ENOTSUP if the request is not async.
 * UV_ENOTSUP if off == -1.
 * UV_ENOTSUP if req->fs_req_engine == UV__ENGINE_THREADPOOL, which means the
 * operation is explicitly required to use the threadpool.
 * UV_ENOMEM if the SQ is full or the CQ might become full.
 * UV_UNKNOWN if no jobs were successfully submitted. (Should not happen.)
 * Any of the errors that may be set by io_uring_enter(2).
 */
int uv__io_uring_fs_work(uint8_t opcode,
                         uv_loop_t* loop,
                         uv_fs_t* req,
                         uv_os_fd_t file,
                         const uv_buf_t bufs[],
                         unsigned int nbufs,
                         int64_t off,
                         uv_fs_cb cb) {
  struct uv__backend_data_io_uring* backend_data;
  struct io_uring_sqe* sqe;
  int submitted;
  uint32_t incr_val;
  uv_poll_t* handle;

  if (cb == NULL || loop == NULL)
    return UV_ENOTSUP;

  if (!(loop->flags & UV_LOOP_USE_IOURING))
    return UV_ENOSYS;

  /* Explicit request to use the threadpool instead. */
  if (req->priv.fs_req_engine == UV__ENGINE_THREADPOOL)
    return UV_ENOTSUP;

  /* io_uring does not support current-position ops, and we can't achieve atomic
   * behavior with lseek(2). TODO it can in Linux 5.4+
   */
  if (off < 0)
    return UV_ENOTSUP;

  backend_data = loop->backend.data;

  /* The CQ is 2x the size of the SQ, but the kernel quickly frees up the slot
   * in the SQ after submission, so we could potentially overflow it if we
   * submit a ton of SQEs in one loop iteration.
   */
  incr_val = (uint32_t)backend_data->pending + 1;
  if (incr_val > *backend_data->ring.sq.kring_entries)
    return UV_ENOMEM;

  sqe = io_uring_get_sqe(&backend_data->ring);
  /* See TODO where #define IOURING_SQ_SIZE is. */
  if (sqe == NULL)
    return UV_ENOMEM;

  sqe->opcode = opcode;
  sqe->fd = file;
  sqe->off = off;
  sqe->addr = (uint64_t)req->bufs; /* TODO 32-bit warning, cast to uintptr_t or unsigned long? */
  sqe->len = nbufs;
  sqe->user_data = (uint64_t)req; /* TODO 32-bit warning, cast to uintptr_t or unsigned long? */

  submitted = io_uring_submit(&backend_data->ring);

  if (submitted == 1) {
    req->priv.fs_req_engine |= UV__ENGINE_IOURING;

    if (backend_data->pending++ == 0) {
      handle = &backend_data->poll_handle;
      uv__io_start(loop, &handle->io_watcher, POLLIN);
      uv__handle_start(handle);
    }

    return 0;
  }

  /* Should not happen; mainly to check we're advancing the rings correctly. */
  if (submitted == 0)
    return UV_UNKNOWN;

  return UV__ERR(errno);
}


int uv__platform_fs_read(uv_loop_t* loop,
                         uv_fs_t* req,
                         uv_os_fd_t file,
                         const uv_buf_t bufs[],
                         unsigned int nbufs,
                         int64_t off,
                         uv_fs_cb cb) {
  return uv__io_uring_fs_work(IORING_OP_READV,
                              loop,
                              req,
                              file,
                              bufs,
                              nbufs,
                              off,
                              cb);
}


int uv__platform_fs_write(uv_loop_t* loop,
                          uv_fs_t* req,
                          uv_os_fd_t file,
                          const uv_buf_t bufs[],
                          unsigned int nbufs,
                          int64_t off,
                          uv_fs_cb cb) {
  return uv__io_uring_fs_work(IORING_OP_WRITEV,
                              loop,
                              req,
                              file,
                              bufs,
                              nbufs,
                              off,
                              cb);
}


int uv__platform_fs_fsync(uv_loop_t* loop,
                          uv_fs_t* req,
                          uv_os_fd_t file,
                          uv_fs_cb cb) {
  return uv__io_uring_fs_work(IORING_OP_FSYNC,
                              loop,
                              req,
                              file,
                              NULL,
                              0,
                              0,
                              cb);
}

/* TODO: openat/openat2 (Linux 5.6) */
/* TODO: close (Linux 5.6) */
/* TODO: statx (Linux 5.6) */

int uv__platform_work_cancel(uv_req_t* req) {
  /* TODO io_uring can cancel in some scenarios now. */
  if (req->type == UV_FS &&
      ((uv_fs_t*)req)->priv.fs_req_engine == UV__ENGINE_IOURING) {
    ((uv_fs_t*)req)->result = UV_ECANCELED;
    return 0;
  }

  return UV_ENOSYS;
}
