/* SPDX-License-Identifier: MIT */
/*
 * Description: Basic IO cancel test
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>

#include "liburing.h"

#define FILE_SIZE	(128 * 1024)
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

static unsigned long long utime_since(const struct timeval *s,
				      const struct timeval *e)
{
	long long sec, usec;

	sec = e->tv_sec - s->tv_sec;
	usec = (e->tv_usec - s->tv_usec);
	if (sec > 0 && usec < 0) {
		sec--;
		usec += 1000000;
	}

	sec *= 1000000;
	return sec + usec;
}

static unsigned long long utime_since_now(struct timeval *tv)
{
	struct timeval end;

	gettimeofday(&end, NULL);
	return utime_since(tv, &end);
}

static int start_io(struct io_uring *ring, int fd, int do_write)
{
	struct io_uring_sqe *sqe;
	int i, ret;

	for (i = 0; i < BUFFERS; i++) {
		off_t offset;

		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		offset = BS * (rand() % BUFFERS);
		if (do_write) {
			io_uring_prep_writev(sqe, fd, &vecs[i], 1, offset);
		} else {
			io_uring_prep_readv(sqe, fd, &vecs[i], 1, offset);
		}
		sqe->user_data = i + 1;
	}

	ret = io_uring_submit(ring);
	if (ret != BUFFERS) {
		fprintf(stderr, "submit got %d, wanted %d\n", ret, BUFFERS);
		goto err;
	}

	return 0;
err:
	return 1;
}

static int wait_io(struct io_uring *ring, unsigned nr_io, int do_partial)
{
	struct io_uring_cqe *cqe;
	int i, ret;

	for (i = 0; i < nr_io; i++) {
		ret = io_uring_wait_cqe(ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		}
		if (do_partial && cqe->user_data) {
			if (!(cqe->user_data & 1)) {
				if (cqe->res != BS) {
					fprintf(stderr, "IO %d wasn't cancelled but got error %d\n", (unsigned) cqe->user_data, cqe->res);
					goto err;
				}
			}
		}
		io_uring_cqe_seen(ring, cqe);
	}
	return 0;
err:
	return 1;

}

static int do_io(struct io_uring *ring, int fd, int do_write)
{
	if (start_io(ring, fd, do_write))
		return 1;
	if (wait_io(ring, BUFFERS, 0))
		return 1;
	return 0;
}

static int start_cancel(struct io_uring *ring, int do_partial)
{
	struct io_uring_sqe *sqe;
	int i, ret, submitted = 0;

	for (i = 0; i < BUFFERS; i++) {
		if (do_partial && (i & 1))
			continue;
		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		io_uring_prep_cancel(sqe, (void *) (unsigned long) i + 1, 0);
		sqe->user_data = 0;
		submitted++;
	}

	ret = io_uring_submit(ring);
	if (ret != submitted) {
		fprintf(stderr, "submit got %d, wanted %d\n", ret, submitted);
		goto err;
	}
	return 0;
err:
	return 1;
}

/*
 * Test cancels. If 'do_partial' is set, then we only attempt to cancel half of
 * the submitted IO. This is done to verify that cancelling one piece of IO doesn't
 * impact others.
 */
static int test_io_cancel(const char *file, int do_write, int do_partial)
{
	struct io_uring ring;
	struct timeval start_tv;
	unsigned long usecs;
	unsigned to_wait;
	int fd, ret;

	fd = open(file, O_RDWR | O_DIRECT);
	if (fd < 0) {
		perror("file open");
		goto err;
	}

	ret = io_uring_queue_init(4 * BUFFERS, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		goto err;
	}

	if (do_io(&ring, fd, do_write))
		goto err;
	gettimeofday(&start_tv, NULL);
	if (do_io(&ring, fd, do_write))
		goto err;
	usecs = utime_since_now(&start_tv);

	if (start_io(&ring, fd, do_write))
		goto err;
	/* sleep for 1/3 of the total time, to allow some to start/complete */
	usleep(usecs / 3);
	if (start_cancel(&ring, do_partial))
		goto err;
	to_wait = BUFFERS;
	if (do_partial)
		to_wait += BUFFERS / 2;
	else
		to_wait += BUFFERS;
	if (wait_io(&ring, to_wait, do_partial))
		goto err;

	io_uring_queue_exit(&ring);
	close(fd);
	return 0;
err:
	if (fd != -1)
		close(fd);
	return 1;
}

int main(int argc, char *argv[])
{
	int i, ret;

	if (argc > 1)
		return 0;

	if (create_file(".basic-rw")) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}
	if (create_buffers()) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}

	for (i = 0; i < 4; i++) {
		int v1 = (i & 1) != 0;
		int v2 = (i & 2) != 0;

		ret = test_io_cancel(".basic-rw", v1, v2);
		if (ret) {
			fprintf(stderr, "test_io_cancel %d %d failed\n", v1, v2);
			goto err;
		}
	}

	unlink(".basic-rw");
	return 0;
err:
	unlink(".basic-rw");
	return 1;
}
