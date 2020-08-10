/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>

#include "liburing.h"

#define BUF_SIZE 4096
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

int main(int argc, char *argv[])
{
	int ret, fd, save_errno;
	struct io_uring ring;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct iovec vec;

	if (argc > 1)
		return 0;

	vec.iov_base = malloc(BUF_SIZE);
	vec.iov_len = BUF_SIZE;

	if (create_file(".short-read")) {
		fprintf(stderr, "file creation failed\n");
		return 1;
	}

	fd = open(".short-read", O_RDONLY);
	save_errno = errno;
	unlink(".short-read");
	errno = save_errno;
	if (fd < 0) {
		perror("file open");
		return 1;
	}

	ret = io_uring_queue_init(32, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		return ret;
	}

	sqe = io_uring_get_sqe(&ring);
	if (!sqe) {
		fprintf(stderr, "sqe get failed\n");
		return 1;
	}
	io_uring_prep_readv(sqe, fd, &vec, 1, 0);

	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "submit failed: %d\n", ret);
 		return 1;
	}

	ret = io_uring_wait_cqes(&ring, &cqe, 1, 0, 0);
	if (ret) {
		fprintf(stderr, "wait_cqe failed: %d\n", ret);
		return 1;
	}

	if (cqe->res != FILE_SIZE) {
		fprintf(stderr, "Read failed: %d\n", cqe->res);
		return 1;
	}

	io_uring_cqe_seen(&ring, cqe);
	return 0;
}
