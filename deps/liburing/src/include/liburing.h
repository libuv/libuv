/* SPDX-License-Identifier: MIT */
#ifndef LIB_URING_H
#define LIB_URING_H

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <linux/swab.h>
#include "liburing/compat.h"
#include "liburing/io_uring.h"
#include "liburing/barrier.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Library interface to io_uring
 */
struct io_uring_sq {
	unsigned *khead;
	unsigned *ktail;
	unsigned *kring_mask;
	unsigned *kring_entries;
	unsigned *kflags;
	unsigned *kdropped;
	unsigned *array;
	struct io_uring_sqe *sqes;

	unsigned sqe_head;
	unsigned sqe_tail;

	size_t ring_sz;
	void *ring_ptr;
};

struct io_uring_cq {
	unsigned *khead;
	unsigned *ktail;
	unsigned *kring_mask;
	unsigned *kring_entries;
	unsigned *kflags;
	unsigned *koverflow;
	struct io_uring_cqe *cqes;

	size_t ring_sz;
	void *ring_ptr;
};

struct io_uring {
	struct io_uring_sq sq;
	struct io_uring_cq cq;
	unsigned flags;
	int ring_fd;
};

/*
 * Library interface
 */

/*
 * return an allocated io_uring_probe structure, or NULL if probe fails (for
 * example, if it is not available). The caller is responsible for freeing it
 */
extern struct io_uring_probe *io_uring_get_probe_ring(struct io_uring *ring);
/* same as io_uring_get_probe_ring, but takes care of ring init and teardown */
extern struct io_uring_probe *io_uring_get_probe(void);

static inline int io_uring_opcode_supported(struct io_uring_probe *p, int op)
{
	if (op > p->last_op)
		return 0;
	return (p->ops[op].flags & IO_URING_OP_SUPPORTED) != 0;
}

extern int io_uring_queue_init_params(unsigned entries, struct io_uring *ring,
	struct io_uring_params *p);
extern int io_uring_queue_init(unsigned entries, struct io_uring *ring,
	unsigned flags);
extern int io_uring_queue_mmap(int fd, struct io_uring_params *p,
	struct io_uring *ring);
extern int io_uring_ring_dontfork(struct io_uring *ring);
extern void io_uring_queue_exit(struct io_uring *ring);
unsigned io_uring_peek_batch_cqe(struct io_uring *ring,
	struct io_uring_cqe **cqes, unsigned count);
extern int io_uring_wait_cqes(struct io_uring *ring,
	struct io_uring_cqe **cqe_ptr, unsigned wait_nr,
	struct __kernel_timespec *ts, sigset_t *sigmask);
extern int io_uring_wait_cqe_timeout(struct io_uring *ring,
	struct io_uring_cqe **cqe_ptr, struct __kernel_timespec *ts);
extern int io_uring_submit(struct io_uring *ring);
extern int io_uring_submit_and_wait(struct io_uring *ring, unsigned wait_nr);
extern struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring);

extern int io_uring_register_buffers(struct io_uring *ring,
					const struct iovec *iovecs,
					unsigned nr_iovecs);
extern int io_uring_unregister_buffers(struct io_uring *ring);
extern int io_uring_register_files(struct io_uring *ring, const int *files,
					unsigned nr_files);
extern int io_uring_unregister_files(struct io_uring *ring);
extern int io_uring_register_files_update(struct io_uring *ring, unsigned off,
					int *files, unsigned nr_files);
extern int io_uring_register_eventfd(struct io_uring *ring, int fd);
extern int io_uring_register_eventfd_async(struct io_uring *ring, int fd);
extern int io_uring_unregister_eventfd(struct io_uring *ring);
extern int io_uring_register_probe(struct io_uring *ring,
					struct io_uring_probe *p, unsigned nr);
extern int io_uring_register_personality(struct io_uring *ring);
extern int io_uring_unregister_personality(struct io_uring *ring, int id);

/*
 * Helper for the peek/wait single cqe functions. Exported because of that,
 * but probably shouldn't be used directly in an application.
 */
extern int __io_uring_get_cqe(struct io_uring *ring,
			      struct io_uring_cqe **cqe_ptr, unsigned submit,
			      unsigned wait_nr, sigset_t *sigmask);

#define LIBURING_UDATA_TIMEOUT	((__u64) -1)

#define io_uring_for_each_cqe(ring, head, cqe)				\
	/*								\
	 * io_uring_smp_load_acquire() enforces the order of tail	\
	 * and CQE reads.						\
	 */								\
	for (head = *(ring)->cq.khead;					\
	     (cqe = (head != io_uring_smp_load_acquire((ring)->cq.ktail) ? \
		&(ring)->cq.cqes[head & (*(ring)->cq.kring_mask)] : NULL)); \
	     head++)							\

/*
 * Must be called after io_uring_for_each_cqe()
 */
static inline void io_uring_cq_advance(struct io_uring *ring,
				       unsigned nr)
{
	if (nr) {
		struct io_uring_cq *cq = &ring->cq;

		/*
		 * Ensure that the kernel only sees the new value of the head
		 * index after the CQEs have been read.
		 */
		io_uring_smp_store_release(cq->khead, *cq->khead + nr);
	}
}

/*
 * Must be called after io_uring_{peek,wait}_cqe() after the cqe has
 * been processed by the application.
 */
static inline void io_uring_cqe_seen(struct io_uring *ring,
				     struct io_uring_cqe *cqe)
{
	if (cqe)
		io_uring_cq_advance(ring, 1);
}

/*
 * Command prep helpers
 */
static inline void io_uring_sqe_set_data(struct io_uring_sqe *sqe, void *data)
{
	sqe->user_data = (unsigned long) data;
}

static inline void *io_uring_cqe_get_data(const struct io_uring_cqe *cqe)
{
	return (void *) (uintptr_t) cqe->user_data;
}

static inline void io_uring_sqe_set_flags(struct io_uring_sqe *sqe,
					  unsigned flags)
{
	sqe->flags = flags;
}

static inline void io_uring_prep_rw(int op, struct io_uring_sqe *sqe, int fd,
				    const void *addr, unsigned len,
				    __u64 offset)
{
	sqe->opcode = op;
	sqe->flags = 0;
	sqe->ioprio = 0;
	sqe->fd = fd;
	sqe->off = offset;
	sqe->addr = (unsigned long) addr;
	sqe->len = len;
	sqe->rw_flags = 0;
	sqe->user_data = 0;
	sqe->__pad2[0] = sqe->__pad2[1] = sqe->__pad2[2] = 0;
}

static inline void io_uring_prep_splice(struct io_uring_sqe *sqe,
					int fd_in, uint64_t off_in,
					int fd_out, uint64_t off_out,
					unsigned int nbytes,
					unsigned int splice_flags)
{
	io_uring_prep_rw(IORING_OP_SPLICE, sqe, fd_out, NULL, nbytes, off_out);
	sqe->splice_off_in = off_in;
	sqe->splice_fd_in = fd_in;
	sqe->splice_flags = splice_flags;
}

static inline void io_uring_prep_readv(struct io_uring_sqe *sqe, int fd,
				       const struct iovec *iovecs,
				       unsigned nr_vecs, off_t offset)
{
	io_uring_prep_rw(IORING_OP_READV, sqe, fd, iovecs, nr_vecs, offset);
}

static inline void io_uring_prep_read_fixed(struct io_uring_sqe *sqe, int fd,
					    void *buf, unsigned nbytes,
					    off_t offset, int buf_index)
{
	io_uring_prep_rw(IORING_OP_READ_FIXED, sqe, fd, buf, nbytes, offset);
	sqe->buf_index = buf_index;
}

static inline void io_uring_prep_writev(struct io_uring_sqe *sqe, int fd,
					const struct iovec *iovecs,
					unsigned nr_vecs, off_t offset)
{
	io_uring_prep_rw(IORING_OP_WRITEV, sqe, fd, iovecs, nr_vecs, offset);
}

static inline void io_uring_prep_write_fixed(struct io_uring_sqe *sqe, int fd,
					     const void *buf, unsigned nbytes,
					     off_t offset, int buf_index)
{
	io_uring_prep_rw(IORING_OP_WRITE_FIXED, sqe, fd, buf, nbytes, offset);
	sqe->buf_index = buf_index;
}

static inline void io_uring_prep_recvmsg(struct io_uring_sqe *sqe, int fd,
					 struct msghdr *msg, unsigned flags)
{
	io_uring_prep_rw(IORING_OP_RECVMSG, sqe, fd, msg, 1, 0);
	sqe->msg_flags = flags;
}

static inline void io_uring_prep_sendmsg(struct io_uring_sqe *sqe, int fd,
					 const struct msghdr *msg, unsigned flags)
{
	io_uring_prep_rw(IORING_OP_SENDMSG, sqe, fd, msg, 1, 0);
	sqe->msg_flags = flags;
}

static inline void io_uring_prep_poll_add(struct io_uring_sqe *sqe, int fd,
					  unsigned poll_mask)
{
	io_uring_prep_rw(IORING_OP_POLL_ADD, sqe, fd, NULL, 0, 0);
#if __BYTE_ORDER == __BIG_ENDIAN
	poll_mask = __swahw32(poll_mask);
#endif
	sqe->poll32_events = poll_mask;
}

static inline void io_uring_prep_poll_remove(struct io_uring_sqe *sqe,
					     void *user_data)
{
	io_uring_prep_rw(IORING_OP_POLL_REMOVE, sqe, -1, user_data, 0, 0);
}

static inline void io_uring_prep_fsync(struct io_uring_sqe *sqe, int fd,
				       unsigned fsync_flags)
{
	io_uring_prep_rw(IORING_OP_FSYNC, sqe, fd, NULL, 0, 0);
	sqe->fsync_flags = fsync_flags;
}

static inline void io_uring_prep_nop(struct io_uring_sqe *sqe)
{
	io_uring_prep_rw(IORING_OP_NOP, sqe, -1, NULL, 0, 0);
}

static inline void io_uring_prep_timeout(struct io_uring_sqe *sqe,
					 struct __kernel_timespec *ts,
					 unsigned count, unsigned flags)
{
	io_uring_prep_rw(IORING_OP_TIMEOUT, sqe, -1, ts, 1, count);
	sqe->timeout_flags = flags;
}

static inline void io_uring_prep_timeout_remove(struct io_uring_sqe *sqe,
						__u64 user_data, unsigned flags)
{
	io_uring_prep_rw(IORING_OP_TIMEOUT_REMOVE, sqe, -1,
				(void *)(unsigned long)user_data, 0, 0);
	sqe->timeout_flags = flags;
}

static inline void io_uring_prep_accept(struct io_uring_sqe *sqe, int fd,
					struct sockaddr *addr,
					socklen_t *addrlen, int flags)
{
	io_uring_prep_rw(IORING_OP_ACCEPT, sqe, fd, addr, 0,
				(__u64) (unsigned long) addrlen);
	sqe->accept_flags = flags;
}

static inline void io_uring_prep_cancel(struct io_uring_sqe *sqe, void *user_data,
					int flags)
{
	io_uring_prep_rw(IORING_OP_ASYNC_CANCEL, sqe, -1, user_data, 0, 0);
	sqe->cancel_flags = flags;
}

static inline void io_uring_prep_link_timeout(struct io_uring_sqe *sqe,
					      struct __kernel_timespec *ts,
					      unsigned flags)
{
	io_uring_prep_rw(IORING_OP_LINK_TIMEOUT, sqe, -1, ts, 1, 0);
	sqe->timeout_flags = flags;
}

static inline void io_uring_prep_connect(struct io_uring_sqe *sqe, int fd,
					 const struct sockaddr *addr,
					 socklen_t addrlen)
{
	io_uring_prep_rw(IORING_OP_CONNECT, sqe, fd, addr, 0, addrlen);
}

static inline void io_uring_prep_files_update(struct io_uring_sqe *sqe,
					      int *fds, unsigned nr_fds,
					      int offset)
{
	io_uring_prep_rw(IORING_OP_FILES_UPDATE, sqe, -1, fds, nr_fds, offset);
}

static inline void io_uring_prep_fallocate(struct io_uring_sqe *sqe, int fd,
					   int mode, off_t offset, off_t len)
{

	io_uring_prep_rw(IORING_OP_FALLOCATE, sqe, fd,
			(const uintptr_t *) (unsigned long) len, mode, offset);
}

static inline void io_uring_prep_openat(struct io_uring_sqe *sqe, int dfd,
					const char *path, int flags, mode_t mode)
{
	io_uring_prep_rw(IORING_OP_OPENAT, sqe, dfd, path, mode, 0);
	sqe->open_flags = flags;
}

static inline void io_uring_prep_close(struct io_uring_sqe *sqe, int fd)
{
	io_uring_prep_rw(IORING_OP_CLOSE, sqe, fd, NULL, 0, 0);
}

static inline void io_uring_prep_read(struct io_uring_sqe *sqe, int fd,
				      void *buf, unsigned nbytes, off_t offset)
{
	io_uring_prep_rw(IORING_OP_READ, sqe, fd, buf, nbytes, offset);
}

static inline void io_uring_prep_write(struct io_uring_sqe *sqe, int fd,
				       const void *buf, unsigned nbytes, off_t offset)
{
	io_uring_prep_rw(IORING_OP_WRITE, sqe, fd, buf, nbytes, offset);
}

struct statx;
static inline void io_uring_prep_statx(struct io_uring_sqe *sqe, int dfd,
				const char *path, int flags, unsigned mask,
				struct statx *statxbuf)
{
	io_uring_prep_rw(IORING_OP_STATX, sqe, dfd, path, mask,
				(__u64) (unsigned long) statxbuf);
	sqe->statx_flags = flags;
}

static inline void io_uring_prep_fadvise(struct io_uring_sqe *sqe, int fd,
					 off_t offset, off_t len, int advice)
{
	io_uring_prep_rw(IORING_OP_FADVISE, sqe, fd, NULL, len, offset);
	sqe->fadvise_advice = advice;
}

static inline void io_uring_prep_madvise(struct io_uring_sqe *sqe, void *addr,
					 off_t length, int advice)
{
	io_uring_prep_rw(IORING_OP_MADVISE, sqe, -1, addr, length, 0);
	sqe->fadvise_advice = advice;
}

static inline void io_uring_prep_send(struct io_uring_sqe *sqe, int sockfd,
				      const void *buf, size_t len, int flags)
{
	io_uring_prep_rw(IORING_OP_SEND, sqe, sockfd, buf, len, 0);
	sqe->msg_flags = flags;
}

static inline void io_uring_prep_recv(struct io_uring_sqe *sqe, int sockfd,
				      void *buf, size_t len, int flags)
{
	io_uring_prep_rw(IORING_OP_RECV, sqe, sockfd, buf, len, 0);
	sqe->msg_flags = flags;
}

static inline void io_uring_prep_openat2(struct io_uring_sqe *sqe, int dfd,
					const char *path, struct open_how *how)
{
	io_uring_prep_rw(IORING_OP_OPENAT2, sqe, dfd, path, sizeof(*how),
				(uint64_t) (uintptr_t) how);
}

struct epoll_event;
static inline void io_uring_prep_epoll_ctl(struct io_uring_sqe *sqe, int epfd,
					   int fd, int op,
					   struct epoll_event *ev)
{
	io_uring_prep_rw(IORING_OP_EPOLL_CTL, sqe, epfd, ev, op, fd);
}

static inline void io_uring_prep_provide_buffers(struct io_uring_sqe *sqe,
						 void *addr, int len, int nr,
						 int bgid, int bid)
{
	io_uring_prep_rw(IORING_OP_PROVIDE_BUFFERS, sqe, nr, addr, len, bid);
	sqe->buf_group = bgid;
}

static inline void io_uring_prep_remove_buffers(struct io_uring_sqe *sqe,
						int nr, int bgid)
{
	io_uring_prep_rw(IORING_OP_REMOVE_BUFFERS, sqe, nr, NULL, 0, 0);
	sqe->buf_group = bgid;
}

static inline unsigned io_uring_sq_ready(struct io_uring *ring)
{
	/* always use real head, to avoid losing sync for short submit */
	return ring->sq.sqe_tail - *ring->sq.khead;
}

static inline unsigned io_uring_sq_space_left(struct io_uring *ring)
{
	return *ring->sq.kring_entries - io_uring_sq_ready(ring);
}

static inline unsigned io_uring_cq_ready(struct io_uring *ring)
{
	return io_uring_smp_load_acquire(ring->cq.ktail) - *ring->cq.khead;
}

static inline bool io_uring_cq_eventfd_enabled(struct io_uring *ring)
{
	if (!ring->cq.kflags)
		return true;

	return !(*ring->cq.kflags & IORING_CQ_EVENTFD_DISABLED);
}

static inline int io_uring_cq_eventfd_toggle(struct io_uring *ring,
					     bool enabled)
{
	uint32_t flags;

	if (!!enabled == io_uring_cq_eventfd_enabled(ring))
		return 0;

	if (!ring->cq.kflags)
		return -EOPNOTSUPP;

	flags = *ring->cq.kflags;

	if (enabled)
		flags &= ~IORING_CQ_EVENTFD_DISABLED;
	else
		flags |= IORING_CQ_EVENTFD_DISABLED;

	IO_URING_WRITE_ONCE(*ring->cq.kflags, flags);

	return 0;
}

/*
 * Return an IO completion, waiting for 'wait_nr' completions if one isn't
 * readily available. Returns 0 with cqe_ptr filled in on success, -errno on
 * failure.
 */
static inline int io_uring_wait_cqe_nr(struct io_uring *ring,
				      struct io_uring_cqe **cqe_ptr,
				      unsigned wait_nr)
{
	return __io_uring_get_cqe(ring, cqe_ptr, 0, wait_nr, NULL);
}

/*
 * Return an IO completion, if one is readily available. Returns 0 with
 * cqe_ptr filled in on success, -errno on failure.
 */
static inline int io_uring_peek_cqe(struct io_uring *ring,
				    struct io_uring_cqe **cqe_ptr)
{
	return io_uring_wait_cqe_nr(ring, cqe_ptr, 0);
}

/*
 * Return an IO completion, waiting for it if necessary. Returns 0 with
 * cqe_ptr filled in on success, -errno on failure.
 */
static inline int io_uring_wait_cqe(struct io_uring *ring,
				    struct io_uring_cqe **cqe_ptr)
{
	return io_uring_wait_cqe_nr(ring, cqe_ptr, 1);
}

#ifdef __cplusplus
}
#endif

#endif
