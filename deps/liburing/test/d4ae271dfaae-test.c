/* SPDX-License-Identifier: MIT */
/*
 * Test case for SQPOLL missing a 'ret' clear in case of busy.
 *
 * Heavily based on a test case from
 * Xiaoguang Wang <xiaoguang.wang@linux.alibaba.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "liburing.h"

#define FILE_SIZE	(128 * 1024)

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
	fsync(fd);
	close(fd);
	return ret != FILE_SIZE;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	int i, fd, ret;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct iovec *iovecs;
	struct io_uring_params p;
	char *fname;
	void *buf;

	if (geteuid()) {
		fprintf(stdout, "Test requires root, skipping\n");
		return 0;
	}

	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_SQPOLL;
	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return 1;
	}

	if (argc > 1) {
		fname = argv[1];
	} else {
		fname = ".sqpoll.tmp";
		if (create_file(fname)) {
			fprintf(stderr, "file creation failed\n");
			goto out;
		}
	}

	fd = open(fname, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		perror("open");
		goto out;
	}

	iovecs = calloc(10, sizeof(struct iovec));
	for (i = 0; i < 10; i++) {
		if (posix_memalign(&buf, 4096, 4096))
			goto out;
		iovecs[i].iov_base = buf;
		iovecs[i].iov_len = 4096;
	}

	ret = io_uring_register_files(&ring, &fd, 1);
	if (ret < 0) {
		fprintf(stderr, "register files %d\n", ret);
		goto out;
	}

	for (i = 0; i < 10; i++) {
		sqe = io_uring_get_sqe(&ring);
		if (!sqe)
			break;

		io_uring_prep_readv(sqe, 0, &iovecs[i], 1, 0);
		sqe->flags |= IOSQE_FIXED_FILE;

		ret = io_uring_submit(&ring);
		usleep(1000);
	}

	for (i = 0; i < 10; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			break;
		}
		if (cqe->res != 4096) {
			fprintf(stderr, "ret=%d, wanted 4096\n", cqe->res);
			ret = 1;
			break;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	close(fd);
out:
	if (fname != argv[1])
		unlink(fname);
	io_uring_queue_exit(&ring);
	return ret;
}
