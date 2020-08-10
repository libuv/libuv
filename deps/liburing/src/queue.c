/* SPDX-License-Identifier: MIT */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "liburing/compat.h"
#include "liburing/io_uring.h"
#include "liburing.h"
#include "liburing/barrier.h"

#include "syscall.h"

/*
 * Returns true if we're not using SQ thread (thus nobody submits but us)
 * or if IORING_SQ_NEED_WAKEUP is set, so submit thread must be explicitly
 * awakened. For the latter case, we set the thread wakeup flag.
 */
static inline bool sq_ring_needs_enter(struct io_uring *ring,
				unsigned submitted, unsigned *flags)
{
	if (!(ring->flags & IORING_SETUP_SQPOLL) && submitted)
		return true;
	if (IO_URING_READ_ONCE(*ring->sq.kflags) & IORING_SQ_NEED_WAKEUP) {
		*flags |= IORING_ENTER_SQ_WAKEUP;
		return true;
	}

	return false;
}

static inline bool cq_ring_needs_flush(struct io_uring *ring)
{
	return IO_URING_READ_ONCE(*ring->sq.kflags) & IORING_SQ_CQ_OVERFLOW;
}

static int __io_uring_peek_cqe(struct io_uring *ring,
			       struct io_uring_cqe **cqe_ptr)
{
	struct io_uring_cqe *cqe;
	unsigned head;
	int err = 0;

	do {
		io_uring_for_each_cqe(ring, head, cqe)
			break;
		if (cqe) {
			if (cqe->user_data == LIBURING_UDATA_TIMEOUT) {
				if (cqe->res < 0)
					err = cqe->res;
				io_uring_cq_advance(ring, 1);
				if (!err)
					continue;
				cqe = NULL;
			}
		}
		break;
	} while (1);

	*cqe_ptr = cqe;
	return err;
}

int __io_uring_get_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr,
		       unsigned submit, unsigned wait_nr, sigset_t *sigmask)
{
	struct io_uring_cqe *cqe = NULL;
	const int to_wait = wait_nr;
	int ret = 0, err;

	do {
		bool cq_overflow_flush = false;
		unsigned flags = 0;

		err = __io_uring_peek_cqe(ring, &cqe);
		if (err)
			break;
		if (!cqe && !to_wait && !submit) {
			if (!cq_ring_needs_flush(ring)) {
				err = -EAGAIN;
				break;
			}
			cq_overflow_flush = true;
		}
		if (wait_nr && cqe)
			wait_nr--;
		if (wait_nr || cq_overflow_flush)
			flags = IORING_ENTER_GETEVENTS;
		if (submit)
			sq_ring_needs_enter(ring, submit, &flags);
		if (wait_nr || submit || cq_overflow_flush)
			ret = __sys_io_uring_enter(ring->ring_fd, submit,
						   wait_nr, flags, sigmask);
		if (ret < 0) {
			err = -errno;
		} else if (ret == (int)submit) {
			submit = 0;
			/*
			 * When SETUP_IOPOLL is set, __sys_io_uring enter()
			 * must be called to reap new completions but the call
			 * won't be made if both wait_nr and submit are zero
			 * so preserve wait_nr.
			 */
			if (!(ring->flags & IORING_SETUP_IOPOLL))
				wait_nr = 0;
		} else {
			submit -= ret;
		}
		if (cqe)
			break;
	} while (!err);

	*cqe_ptr = cqe;
	return err;
}

/*
 * Fill in an array of IO completions up to count, if any are available.
 * Returns the amount of IO completions filled.
 */
unsigned io_uring_peek_batch_cqe(struct io_uring *ring,
				 struct io_uring_cqe **cqes, unsigned count)
{
	unsigned ready;
	bool overflow_checked = false;

again:
	ready = io_uring_cq_ready(ring);
	if (ready) {
		unsigned head = *ring->cq.khead;
		unsigned mask = *ring->cq.kring_mask;
		unsigned last;
		int i = 0;

		count = count > ready ? ready : count;
		last = head + count;
		for (;head != last; head++, i++)
			cqes[i] = &ring->cq.cqes[head & mask];

		return count;
	}

	if (overflow_checked)
		goto done;

	if (cq_ring_needs_flush(ring)) {
		__sys_io_uring_enter(ring->ring_fd, 0, 0,
				     IORING_ENTER_GETEVENTS, NULL);
		overflow_checked = true;
		goto again;
	}

done:
	return 0;
}

/*
 * Sync internal state with kernel ring state on the SQ side. Returns the
 * number of pending items in the SQ ring, for the shared ring.
 */
static int __io_uring_flush_sq(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;
	const unsigned mask = *sq->kring_mask;
	unsigned ktail, to_submit;

	if (sq->sqe_head == sq->sqe_tail) {
		ktail = *sq->ktail;
		goto out;
	}

	/*
	 * Fill in sqes that we have queued up, adding them to the kernel ring
	 */
	ktail = *sq->ktail;
	to_submit = sq->sqe_tail - sq->sqe_head;
	while (to_submit--) {
		sq->array[ktail & mask] = sq->sqe_head & mask;
		ktail++;
		sq->sqe_head++;
	}

	/*
	 * Ensure that the kernel sees the SQE updates before it sees the tail
	 * update.
	 */
	io_uring_smp_store_release(sq->ktail, ktail);
out:
	return ktail - *sq->khead;
}

/*
 * Like io_uring_wait_cqe(), except it accepts a timeout value as well. Note
 * that an sqe is used internally to handle the timeout. Applications using
 * this function must never set sqe->user_data to LIBURING_UDATA_TIMEOUT!
 *
 * If 'ts' is specified, the application need not call io_uring_submit() before
 * calling this function, as we will do that on its behalf. From this it also
 * follows that this function isn't safe to use for applications that split SQ
 * and CQ handling between two threads and expect that to work without
 * synchronization, as this function manipulates both the SQ and CQ side.
 */
int io_uring_wait_cqes(struct io_uring *ring, struct io_uring_cqe **cqe_ptr,
		       unsigned wait_nr, struct __kernel_timespec *ts,
		       sigset_t *sigmask)
{
	unsigned to_submit = 0;

	if (ts) {
		struct io_uring_sqe *sqe;
		int ret;

		/*
		 * If the SQ ring is full, we may need to submit IO first
		 */
		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			ret = io_uring_submit(ring);
			if (ret < 0)
				return ret;
			sqe = io_uring_get_sqe(ring);
			if (!sqe)
				return -EAGAIN;
		}
		io_uring_prep_timeout(sqe, ts, wait_nr, 0);
		sqe->user_data = LIBURING_UDATA_TIMEOUT;
		to_submit = __io_uring_flush_sq(ring);
	}

	return __io_uring_get_cqe(ring, cqe_ptr, to_submit, wait_nr, sigmask);
}

/*
 * See io_uring_wait_cqes() - this function is the same, it just always uses
 * '1' as the wait_nr.
 */
int io_uring_wait_cqe_timeout(struct io_uring *ring,
			      struct io_uring_cqe **cqe_ptr,
			      struct __kernel_timespec *ts)
{
	return io_uring_wait_cqes(ring, cqe_ptr, 1, ts, NULL);
}

/*
 * Submit sqes acquired from io_uring_get_sqe() to the kernel.
 *
 * Returns number of sqes submitted
 */
static int __io_uring_submit(struct io_uring *ring, unsigned submitted,
			     unsigned wait_nr)
{
	unsigned flags;
	int ret;

	flags = 0;
	if (sq_ring_needs_enter(ring, submitted, &flags) || wait_nr) {
		if (wait_nr || (ring->flags & IORING_SETUP_IOPOLL))
			flags |= IORING_ENTER_GETEVENTS;

		ret = __sys_io_uring_enter(ring->ring_fd, submitted, wait_nr,
						flags, NULL);
		if (ret < 0)
			return -errno;
	} else
		ret = submitted;

	return ret;
}

static int __io_uring_submit_and_wait(struct io_uring *ring, unsigned wait_nr)
{
	return __io_uring_submit(ring, __io_uring_flush_sq(ring), wait_nr);
}

/*
 * Submit sqes acquired from io_uring_get_sqe() to the kernel.
 *
 * Returns number of sqes submitted
 */
int io_uring_submit(struct io_uring *ring)
{
	return __io_uring_submit_and_wait(ring, 0);
}

/*
 * Like io_uring_submit(), but allows waiting for events as well.
 *
 * Returns number of sqes submitted
 */
int io_uring_submit_and_wait(struct io_uring *ring, unsigned wait_nr)
{
	return __io_uring_submit_and_wait(ring, wait_nr);
}

static inline struct io_uring_sqe *
__io_uring_get_sqe(struct io_uring_sq *sq, unsigned int __head)
{
	unsigned int __next = (sq)->sqe_tail + 1;
	struct io_uring_sqe *__sqe = NULL;

	if (__next - __head <= *(sq)->kring_entries) {
		__sqe = &(sq)->sqes[(sq)->sqe_tail & *(sq)->kring_mask];
		(sq)->sqe_tail = __next;
	}
	return __sqe;
}

/*
 * Return an sqe to fill. Application must later call io_uring_submit()
 * when it's ready to tell the kernel about it. The caller may call this
 * function multiple times before calling io_uring_submit().
 *
 * Returns a vacant sqe, or NULL if we're full.
 */
struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;

	return __io_uring_get_sqe(sq, io_uring_smp_load_acquire(sq->khead));
}
