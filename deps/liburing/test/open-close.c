/* SPDX-License-Identifier: MIT */
/*
 * Description: run various openat(2) tests
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "liburing.h"

static int create_file(const char *file, size_t size)
{
	ssize_t ret;
	char *buf;
	int fd;

	buf = malloc(size);
	memset(buf, 0xaa, size);

	fd = open(file, O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror("open file");
		return 1;
	}
	ret = write(fd, buf, size);
	close(fd);
	return ret != size;
}

static int test_close(struct io_uring *ring, int fd, int is_ring_fd)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}
	io_uring_prep_close(sqe, fd);

	ret = io_uring_submit(ring);
	if (ret <= 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret < 0) {
		if (!(is_ring_fd && ret == -EBADF)) {
			fprintf(stderr, "wait completion %d\n", ret);
			goto err;
		}
		return ret;
	}
	ret = cqe->res;
	io_uring_cqe_seen(ring, cqe);
	return ret;
err:
	return -1;
}

static int test_openat(struct io_uring *ring, const char *path, int dfd)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int ret;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		goto err;
	}
	io_uring_prep_openat(sqe, dfd, path, O_RDONLY, 0);

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
	ret = cqe->res;
	io_uring_cqe_seen(ring, cqe);
	return ret;
err:
	return -1;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	const char *path, *path_rel;
	int ret, do_unlink;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed\n");
		return 1;
	}

	if (argc > 1) {
		path = "/tmp/.open.close";
		path_rel = argv[1];
		do_unlink = 0;
	} else {
		path = "/tmp/.open.close";
		path_rel = ".open.close";
		do_unlink = 1;
	}

	if (create_file(path, 4096)) {
		fprintf(stderr, "file create failed\n");
		return 1;
	}
	if (do_unlink && create_file(path_rel, 4096)) {
		fprintf(stderr, "file create failed\n");
		return 1;
	}

	ret = test_openat(&ring, path, -1);
	if (ret < 0) {
		if (ret == -EINVAL) {
			fprintf(stdout, "Open not supported, skipping\n");
			goto done;
		}
		fprintf(stderr, "test_openat absolute failed: %d\n", ret);
		goto err;
	}

	ret = test_openat(&ring, path_rel, AT_FDCWD);
	if (ret < 0) {
		fprintf(stderr, "test_openat relative failed: %d\n", ret);
		goto err;
	}

	ret = test_close(&ring, ret, 0);
	if (ret) {
		fprintf(stderr, "test_close normal failed\n");
		goto err;
	}

	ret = test_close(&ring, ring.ring_fd, 1);
	if (ret != -EBADF) {
		fprintf(stderr, "test_close ring_fd failed\n");
		goto err;
	}

done:
	unlink(path);
	if (do_unlink)
		unlink(path_rel);
	return 0;
err:
	unlink(path);
	if (do_unlink)
		unlink(path_rel);
	return 1;
}
