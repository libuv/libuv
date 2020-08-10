/*
 * Check if the kernel sets IORING_SQ_CQ_OVERFLOW so that peeking events
 * still enter the kernel to flush events, if the CQ side is overflown.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "liburing.h"

static int test_cq_overflow(struct io_uring *ring)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	unsigned flags;
	int issued = 0;
	int ret = 0;

	do {
		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			fprintf(stderr, "get sqe failed\n");
			goto err;
		}
		ret = io_uring_submit(ring);
		if (ret <= 0) {
			if (ret != -EBUSY)
				fprintf(stderr, "sqe submit failed: %d\n", ret);
			break;
		}
		issued++;
	} while (ret > 0);

	assert(ret == -EBUSY);

	flags = IO_URING_READ_ONCE(*ring->sq.kflags);
	if (!(flags & IORING_SQ_CQ_OVERFLOW)) {
		fprintf(stdout, "OVERFLOW not set on -EBUSY, skipping\n");
		goto done;
	}

	while (issued) {
		ret = io_uring_peek_cqe(ring, &cqe);
		if (ret) {
			if (ret != -EAGAIN) {
				fprintf(stderr, "peek completion failed: %s\n",
					strerror(ret));
				break;
			}
			continue;
		}
		io_uring_cqe_seen(ring, cqe);
		issued--;
	}

done:
	return 0;
err:
	return 1;
}

int main(int argc, char *argv[])
{
	int ret;
	struct io_uring ring;

	if (argc > 1)
		return 0;

	ret = io_uring_queue_init(16, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed: %d\n", ret);
		return 1;
	}

	ret = test_cq_overflow(&ring);
	if (ret) {
		fprintf(stderr, "test_cq_overflow failed\n");
		return 1;
	}

	return 0;
}
