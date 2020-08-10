/* SPDX-License-Identifier: MIT */
/*
 * Description: run various CQ ring overflow tests
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "liburing.h"

#define FILE_SIZE	(256 * 1024)
#define BS		4096
#define BUFFERS		(FILE_SIZE / BS)

static struct iovec *vecs;

static int create_buffers(void)
{
	int i;

	vecs = malloc(BUFFERS * sizeof(struct iovec));
	for (i = 0; i < BUFFERS; i++) {
		if (posix_memalign(&vecs[i].iov_base, BS, BS))
			return 1;
		vecs[i].iov_len = BS;
	}

	return 0;
}

static int create_file(const char *file)
{
	ssize_t ret;
	char *buf;
	int fd;

	buf = malloc(FILE_SIZE);
	memset(buf, 0xaa, FILE_SIZE);

	fd = open(file, O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror("open file");
		return 1;
	}
	ret = write(fd, buf, FILE_SIZE);
	close(fd);
	return ret != FILE_SIZE;
}

#define ENTRIES	8

static int test_io(const char *file, unsigned long usecs, unsigned *drops, int fault)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring_params p;
	unsigned reaped, total;
	struct io_uring ring;
	int nodrop, i, fd, ret;

	fd = open(file, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		perror("file open");
		goto err;
	}

	memset(&p, 0, sizeof(p));
	ret = io_uring_queue_init_params(ENTRIES, &ring, &p);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		goto err;
	}
	nodrop = 0;
	if (p.features & IORING_FEAT_NODROP)
		nodrop = 1;

	total = 0;
	for (i = 0; i < BUFFERS / 2; i++) {
		off_t offset;

		sqe = io_uring_get_sqe(&ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		offset = BS * (rand() % BUFFERS);
		if (fault && i == ENTRIES + 4)
			vecs[i].iov_base = NULL;
		io_uring_prep_readv(sqe, fd, &vecs[i], 1, offset);

		ret = io_uring_submit(&ring);
		if (nodrop && ret == -EBUSY) {
			*drops = 1;
			total = i;
			break;
		} else if (ret != 1) {
			fprintf(stderr, "submit got %d, wanted %d\n", ret, 1);
			total = i;
			break;
		}
		total++;
	}

	if (*drops)
		goto reap_it;

	usleep(usecs);

	for (i = total; i < BUFFERS; i++) {
		off_t offset;

		sqe = io_uring_get_sqe(&ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		offset = BS * (rand() % BUFFERS);
		io_uring_prep_readv(sqe, fd, &vecs[i], 1, offset);

		ret = io_uring_submit(&ring);
		if (nodrop && ret == -EBUSY) {
			*drops = 1;
			break;
		} else if (ret != 1) {
			fprintf(stderr, "submit got %d, wanted %d\n", ret, 1);
			break;
		}
		total++;
	}

reap_it:
	reaped = 0;
	do {
		if (nodrop) {
			/* nodrop should never lose events */
			if (reaped == total)
				break;
		} else {
			if (reaped + *ring.cq.koverflow == total)
				break;
		}
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		}
		if (cqe->res != BS) {
			if (!(fault && cqe->res == -EFAULT)) {
				fprintf(stderr, "cqe res %d, wanted %d\n",
						cqe->res, BS);
				goto err;
			}
		}
		io_uring_cqe_seen(&ring, cqe);
		reaped++;
	} while (1);

	if (!io_uring_peek_cqe(&ring, &cqe)) {
		fprintf(stderr, "found unexpected completion\n");
		goto err;
	}

	if (!nodrop) {
		*drops = *ring.cq.koverflow;
	} else if (*ring.cq.koverflow) {
		fprintf(stderr, "Found %u overflows\n", *ring.cq.koverflow);
		goto err;
	}

	io_uring_queue_exit(&ring);
	close(fd);
	return 0;
err:
	if (fd != -1)
		close(fd);
	io_uring_queue_exit(&ring);
	return 1;
}

static int reap_events(struct io_uring *ring, unsigned nr_events, int do_wait)
{
	struct io_uring_cqe *cqe;
	int i, ret = 0, seq = 0;

	for (i = 0; i < nr_events; i++) {
		if (do_wait)
			ret = io_uring_wait_cqe(ring, &cqe);
		else
			ret = io_uring_peek_cqe(ring, &cqe);
		if (ret) {
			if (ret != -EAGAIN)
				fprintf(stderr, "cqe peek failed: %d\n", ret);
			break;
		}
		if (cqe->user_data != seq) {
			fprintf(stderr, "cqe sequence out-of-order\n");
			fprintf(stderr, "got %d, wanted %d\n", (int) cqe->user_data,
					seq);
			return -EINVAL;
		}
		seq++;
		io_uring_cqe_seen(ring, cqe);
	}

	return i ? i : ret;
}

/*
 * Setup ring with CQ_NODROP and check we get -EBUSY on trying to submit new IO
 * on an overflown ring, and that we get all the events (even overflows) when
 * we finally reap them.
 */
static int test_overflow_nodrop(void)
{
	struct __kernel_timespec ts;
	struct io_uring_sqe *sqe;
	struct io_uring_params p;
	struct io_uring ring;
	unsigned pending;
	int ret, i, j;

	memset(&p, 0, sizeof(p));
	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret) {
		fprintf(stderr, "io_uring_queue_init failed %d\n", ret);
		return 1;
	}
	if (!(p.features & IORING_FEAT_NODROP)) {
		fprintf(stdout, "FEAT_NODROP not supported, skipped\n");
		return 0;
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 10000000;

	/* submit 4x4 SQEs, should overflow the ring by 8 */
	pending = 0;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			sqe = io_uring_get_sqe(&ring);
			if (!sqe) {
				fprintf(stderr, "get sqe failed\n");
				goto err;
			}

			io_uring_prep_timeout(sqe, &ts, -1U, 0);
			sqe->user_data = (i * 4) + j;
		}

		ret = io_uring_submit(&ring);
		if (ret <= 0) {
			if (ret == -EBUSY)
				break;
			fprintf(stderr, "sqe submit failed: %d, %d\n", ret, pending);
			goto err;
		}
		pending += ret;
	}

	/* wait for timers to fire */
	usleep(2 * 10000);

	/*
	 * We should have 16 pending CQEs now, 8 of them in the overflow list. Any
	 * attempt to queue more IO should return -EBUSY
	 */
	sqe = io_uring_get_sqe(&ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}

	io_uring_prep_nop(sqe);
	ret = io_uring_submit(&ring);
	if (ret != -EBUSY) {
		fprintf(stderr, "expected sqe submit busy: %d\n", ret);
		goto err;
	}

	/* reap the events we should have available */
	ret = reap_events(&ring, pending, 1);
	if (ret < 0) {
		fprintf(stderr, "ret=%d\n", ret);
		goto err;
	}

	if (*ring.cq.koverflow) {
		fprintf(stderr, "cq ring overflow %d, expected 0\n",
				*ring.cq.koverflow);
		goto err;
	}

	io_uring_queue_exit(&ring);
	return 0;
err:
	io_uring_queue_exit(&ring);
	return 1;
}

/*
 * Submit some NOPs and watch if the overflow is correct
 */
static int test_overflow(void)
{
	struct io_uring ring;
	struct io_uring_params p;
	struct io_uring_sqe *sqe;
	unsigned pending;
	int ret, i, j;

	memset(&p, 0, sizeof(p));
	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret) {
		fprintf(stderr, "io_uring_queue_init failed %d\n", ret);
		return 1;
	}

	/* submit 4x4 SQEs, should overflow the ring by 8 */
	pending = 0;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			sqe = io_uring_get_sqe(&ring);
			if (!sqe) {
				fprintf(stderr, "get sqe failed\n");
				goto err;
			}

			io_uring_prep_nop(sqe);
			sqe->user_data = (i * 4) + j;
		}

		ret = io_uring_submit(&ring);
		if (ret == 4) {
			pending += 4;
			continue;
		}
		if (p.features & IORING_FEAT_NODROP) {
			if (ret == -EBUSY)
				break;
		}
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	/* we should now have 8 completions ready */
	ret = reap_events(&ring, pending, 0);
	if (ret < 0)
		goto err;

	if (!(p.features & IORING_FEAT_NODROP)) {
		if (*ring.cq.koverflow != 8) {
			fprintf(stderr, "cq ring overflow %d, expected 8\n",
					*ring.cq.koverflow);
			goto err;
		}
	}
	io_uring_queue_exit(&ring);
	return 0;
err:
	io_uring_queue_exit(&ring);
	return 1;
}

/*
 * Test attempted submit with overflown cq ring that can't get flushed
 */
static int test_overflow_nodrop_submit_ebusy(void)
{
	struct __kernel_timespec ts;
	struct io_uring_sqe *sqe;
	struct io_uring_params p;
	struct io_uring ring;
	unsigned pending;
	int ret, i, j;

	memset(&p, 0, sizeof(p));
	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret) {
		fprintf(stderr, "io_uring_queue_init failed %d\n", ret);
		return 1;
	}
	if (!(p.features & IORING_FEAT_NODROP)) {
		fprintf(stdout, "FEAT_NODROP not supported, skipped\n");
		return 0;
	}

	ts.tv_sec = 1;
	ts.tv_nsec = 0;

	/* submit 4x4 SQEs, should overflow the ring by 8 */
	pending = 0;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			sqe = io_uring_get_sqe(&ring);
			if (!sqe) {
				fprintf(stderr, "get sqe failed\n");
				goto err;
			}

			io_uring_prep_timeout(sqe, &ts, -1U, 0);
			sqe->user_data = (i * 4) + j;
		}

		ret = io_uring_submit(&ring);
		if (ret <= 0) {
			fprintf(stderr, "sqe submit failed: %d, %d\n", ret, pending);
			goto err;
		}
		pending += ret;
	}

	/* wait for timers to fire */
	usleep(1100000);

	/*
	 * We should have 16 pending CQEs now, 8 of them in the overflow list. Any
	 * attempt to queue more IO should return -EBUSY
	 */
	sqe = io_uring_get_sqe(&ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}

	io_uring_prep_nop(sqe);
	ret = io_uring_submit(&ring);
	if (ret != -EBUSY) {
		fprintf(stderr, "expected sqe submit busy: %d\n", ret);
		goto err;
	}

	/*
	 * Now peek existing events so the CQ ring is empty, apart from the
	 * backlog
	 */
	ret = reap_events(&ring, pending, 0);
	if (ret < 0) {
		fprintf(stderr, "ret=%d\n", ret);
		goto err;
	} else if (ret < 8) {
		fprintf(stderr, "only found %d events, expected 8\n", ret);
		goto err;
	}

	/*
	 * We should now be able to submit our previous nop that's still
	 * in the sq ring, as the kernel can flush the existing backlog
	 * to the now empty CQ ring.
	 */
	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "submit got %d, expected 1\n", ret);
		goto err;
	}

	io_uring_queue_exit(&ring);
	return 0;
err:
	io_uring_queue_exit(&ring);
	return 1;
}


int main(int argc, char *argv[])
{
	unsigned iters, drops;
	unsigned long usecs;
	int ret;

	if (argc > 1)
		return 0;

	ret = test_overflow();
	if (ret) {
		printf("test_overflow failed\n");
		return ret;
	}

	ret = test_overflow_nodrop();
	if (ret) {
		printf("test_overflow_nodrop failed\n");
		return ret;
	}

	ret = test_overflow_nodrop_submit_ebusy();
	if (ret) {
		fprintf(stderr, "test_overflow_npdrop_submit_ebusy failed\n");
		return ret;
	}

	if (create_file(".basic-rw")) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}
	if (create_buffers()) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}

	iters = 0;
	usecs = 1000;
	do {
		drops = 0;

		if (test_io(".basic-rw", usecs, &drops, 0)) {
			fprintf(stderr, "test_io nofault failed\n");
			goto err;
		}
		if (drops)
			break;
		usecs = (usecs * 12) / 10;
		iters++;
	} while (iters < 40);

	if (test_io(".basic-rw", usecs, &drops, 0)) {
		fprintf(stderr, "test_io nofault failed\n");
		goto err;
	}

	if (test_io(".basic-rw", usecs, &drops, 1)) {
		fprintf(stderr, "test_io fault failed\n");
		goto err;
	}

	unlink(".basic-rw");
	return 0;
err:
	unlink(".basic-rw");
	return 1;
}
