/* SPDX-License-Identifier: MIT */
/*
 * Description: test io_uring fsync handling
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "liburing.h"

static int test_single_fsync(struct io_uring *ring)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	char buf[32];
	int fd, ret;

	sprintf(buf, "./XXXXXX");
	fd = mkstemp(buf);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}

	io_uring_prep_fsync(sqe, fd, 0);

	ret = io_uring_submit(ring);
	if (ret <= 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret < 0) {
		fprintf(stderr, "wait completion %d\n", ret);
		goto err;
	}

	io_uring_cqe_seen(ring, cqe);
	unlink(buf);
	return 0;
err:
	unlink(buf);
	return 1;
}

static int test_barrier_fsync(struct io_uring *ring)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	struct iovec iovecs[4];
	int i, fd, ret;
	off_t off;

	fd = open("testfile", O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	for (i = 0; i < 4; i++) {
		iovecs[i].iov_base = malloc(4096);
		iovecs[i].iov_len = 4096;
	}

	off = 0;
	for (i = 0; i < 4; i++) {
		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			fprintf(stderr, "get sqe failed\n");
			goto err;
		}

		io_uring_prep_writev(sqe, fd, &iovecs[i], 1, off);
		sqe->user_data = 0;
		off += 4096;
	}

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}

	io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
	sqe->user_data = 1;
	io_uring_sqe_set_flags(sqe, IOSQE_IO_DRAIN);

	ret = io_uring_submit(ring);
	if (ret < 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	} else if (ret < 5) {
		fprintf(stderr, "Submitted only %d\n", ret);
		goto err;
	}

	for (i = 0; i < 5; i++) {
		ret = io_uring_wait_cqe(ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "wait completion %d\n", ret);
			goto err;
		}
		/* kernel doesn't support IOSQE_IO_DRAIN */
		if (cqe->res == -EINVAL)
			break;
		if (i <= 3) {
			if (cqe->user_data) {
				fprintf(stderr, "Got fsync early?\n");
				goto err;
			}
		} else {
			if (!cqe->user_data) {
				fprintf(stderr, "Got write late?\n");
				goto err;
			}
		}
		io_uring_cqe_seen(ring, cqe);
	}

	unlink("testfile");
	return 0;
err:
	unlink("testfile");
	return 1;
}

#define FILE_SIZE 1024

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

static int test_sync_file_range(struct io_uring *ring)
{
	int ret, fd, save_errno;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;

	if (create_file(".sync_file_range")) {
		fprintf(stderr, "file creation failed\n");
		return 1;
	}

	fd = open(".sync_file_range", O_RDWR);
	save_errno = errno;
	unlink(".sync_file_range");
	errno = save_errno;
	if (fd < 0) {
		perror("file open");
		return 1;
	}

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "sqe get failed\n");
		return 1;
	}
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_SYNC_FILE_RANGE;
	sqe->off = 0;
	sqe->len = 0;
	sqe->sync_range_flags = 0;
	sqe->user_data = 1;
	sqe->fd = fd;

	ret = io_uring_submit(ring);
	if (ret != 1) {
		fprintf(stderr, "submit failed: %d\n", ret);
		return 1;
	}
	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret) {
		fprintf(stderr, "wait_cqe failed: %d\n", ret);
		return 1;
	}
	if (cqe->res) {
		fprintf(stderr, "sfr failed: %d\n", cqe->res);
		return 1;
	}

	io_uring_cqe_seen(ring, cqe);
	return 0;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	int ret;

	if (argc > 1)
		return 0;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed\n");
		return 1;

	}

	ret = test_single_fsync(&ring);
	if (ret) {
		fprintf(stderr, "test_single_fsync failed\n");
		return ret;
	}

	ret = test_barrier_fsync(&ring);
	if (ret) {
		fprintf(stderr, "test_barrier_fsync failed\n");
		return ret;
	}

	ret = test_sync_file_range(&ring);
	if (ret) {
		fprintf(stderr, "test_sync_file_range failed\n");
		return ret;
	}

	return 0;
}
