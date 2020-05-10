/* SPDX-License-Identifier: MIT */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "liburing/compat.h"
#include "liburing/io_uring.h"
#include "liburing.h"

#include "syscall.h"

static void io_uring_unmap_rings(struct io_uring_sq *sq, struct io_uring_cq *cq)
{
	munmap(sq->ring_ptr, sq->ring_sz);
	if (cq->ring_ptr && cq->ring_ptr != sq->ring_ptr)
		munmap(cq->ring_ptr, cq->ring_sz);
}

static int io_uring_mmap(int fd, struct io_uring_params *p,
			 struct io_uring_sq *sq, struct io_uring_cq *cq)
{
	size_t size;
	int ret;

	sq->ring_sz = p->sq_off.array + p->sq_entries * sizeof(unsigned);
	cq->ring_sz = p->cq_off.cqes + p->cq_entries * sizeof(struct io_uring_cqe);

	if (p->features & IORING_FEAT_SINGLE_MMAP) {
		if (cq->ring_sz > sq->ring_sz)
			sq->ring_sz = cq->ring_sz;
		cq->ring_sz = sq->ring_sz;
	}
	sq->ring_ptr = mmap(0, sq->ring_sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
	if (sq->ring_ptr == MAP_FAILED)
		return -errno;

	if (p->features & IORING_FEAT_SINGLE_MMAP) {
		cq->ring_ptr = sq->ring_ptr;
	} else {
		cq->ring_ptr = mmap(0, cq->ring_sz, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
		if (cq->ring_ptr == MAP_FAILED) {
			cq->ring_ptr = NULL;
			ret = -errno;
			goto err;
		}
	}

	sq->khead = sq->ring_ptr + p->sq_off.head;
	sq->ktail = sq->ring_ptr + p->sq_off.tail;
	sq->kring_mask = sq->ring_ptr + p->sq_off.ring_mask;
	sq->kring_entries = sq->ring_ptr + p->sq_off.ring_entries;
	sq->kflags = sq->ring_ptr + p->sq_off.flags;
	sq->kdropped = sq->ring_ptr + p->sq_off.dropped;
	sq->array = sq->ring_ptr + p->sq_off.array;

	size = p->sq_entries * sizeof(struct io_uring_sqe);
	sq->sqes = mmap(0, size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd,
				IORING_OFF_SQES);
	if (sq->sqes == MAP_FAILED) {
		ret = -errno;
err:
		io_uring_unmap_rings(sq, cq);
		return ret;
	}

	cq->khead = cq->ring_ptr + p->cq_off.head;
	cq->ktail = cq->ring_ptr + p->cq_off.tail;
	cq->kring_mask = cq->ring_ptr + p->cq_off.ring_mask;
	cq->kring_entries = cq->ring_ptr + p->cq_off.ring_entries;
	cq->koverflow = cq->ring_ptr + p->cq_off.overflow;
	cq->cqes = cq->ring_ptr + p->cq_off.cqes;
	return 0;
}

/*
 * For users that want to specify sq_thread_cpu or sq_thread_idle, this
 * interface is a convenient helper for mmap()ing the rings.
 * Returns -1 on error, or zero on success.  On success, 'ring'
 * contains the necessary information to read/write to the rings.
 */
int io_uring_queue_mmap(int fd, struct io_uring_params *p, struct io_uring *ring)
{
	int ret;

	memset(ring, 0, sizeof(*ring));
	ret = io_uring_mmap(fd, p, &ring->sq, &ring->cq);
	if (!ret) {
		ring->flags = p->flags;
		ring->ring_fd = fd;
	}
	return ret;
}

/*
 * Ensure that the mmap'ed rings aren't available to a child after a fork(2).
 * This uses madvise(..., MADV_DONTFORK) on the mmap'ed ranges.
 */
int io_uring_ring_dontfork(struct io_uring *ring)
{
	size_t len;
	int ret;

	if (!ring->sq.ring_ptr || !ring->sq.sqes || !ring->cq.ring_ptr)
		return -EINVAL;

	len = *ring->sq.kring_entries * sizeof(struct io_uring_sqe);
	ret = madvise(ring->sq.sqes, len, MADV_DONTFORK);
	if (ret == -1)
		return -errno;

	len = ring->sq.ring_sz;
	ret = madvise(ring->sq.ring_ptr, len, MADV_DONTFORK);
	if (ret == -1)
		return -errno;

	if (ring->cq.ring_ptr != ring->sq.ring_ptr) {
		len = ring->cq.ring_sz;
		ret = madvise(ring->cq.ring_ptr, len, MADV_DONTFORK);
		if (ret == -1)
			return -errno;
	}

	return 0;
}

int io_uring_queue_init_params(unsigned entries, struct io_uring *ring,
			       struct io_uring_params *p)
{
	int fd, ret;

	fd = __sys_io_uring_setup(entries, p);
	if (fd < 0)
		return -errno;

	ret = io_uring_queue_mmap(fd, p, ring);
	if (ret)
		close(fd);

	return ret;
}

/*
 * Returns -1 on error, or zero on success. On success, 'ring'
 * contains the necessary information to read/write to the rings.
 */
int io_uring_queue_init(unsigned entries, struct io_uring *ring, unsigned flags)
{
	struct io_uring_params p;

	memset(&p, 0, sizeof(p));
	p.flags = flags;

	return io_uring_queue_init_params(entries, ring, &p);
}

void io_uring_queue_exit(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;
	struct io_uring_cq *cq = &ring->cq;

	munmap(sq->sqes, *sq->kring_entries * sizeof(struct io_uring_sqe));
	io_uring_unmap_rings(sq, cq);
	close(ring->ring_fd);
}

struct io_uring_probe *io_uring_get_probe_ring(struct io_uring *ring)
{
	struct io_uring_probe *probe;
	int r;

	size_t len = sizeof(*probe) + 256 * sizeof(struct io_uring_probe_op);
	probe = malloc(len);
	memset(probe, 0, len);
	r = io_uring_register_probe(ring, probe, 256);
	if (r < 0)
		goto fail;

	return probe;
fail:
	free(probe);
	return NULL;
}

struct io_uring_probe *io_uring_get_probe(void)
{
	struct io_uring ring;
	struct io_uring_probe* probe = NULL;

	int r = io_uring_queue_init(2, &ring, 0);
	if (r < 0)
		return NULL;

	probe = io_uring_get_probe_ring(&ring);
	io_uring_queue_exit(&ring);
	return probe;
}
