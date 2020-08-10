/* SPDX-License-Identifier: MIT */
/*
 * Description: basic read/write tests with polled IO
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include "liburing.h"

#define FILE_SIZE	(128 * 1024)
#define BS		4096
#define BUFFERS		(FILE_SIZE / BS)

static struct iovec *vecs;
static int no_buf_select;
static int no_iopoll;

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

static int provide_buffers(struct io_uring *ring)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int ret, i;

	for (i = 0; i < BUFFERS; i++) {
		sqe = io_uring_get_sqe(ring);
		io_uring_prep_provide_buffers(sqe, vecs[i].iov_base,
						vecs[i].iov_len, 1, 1, i);
	}

	ret = io_uring_submit(ring);
	if (ret != BUFFERS) {
		fprintf(stderr, "submit: %d\n", ret);
		return 1;
	}

	for (i = 0; i < BUFFERS; i++) {
		ret = io_uring_wait_cqe(ring, &cqe);
		if (cqe->res < 0) {
			fprintf(stderr, "cqe->res=%d\n", cqe->res);
			return 1;
		}
		io_uring_cqe_seen(ring, cqe);
	}

	return 0;
}

static int __test_io(const char *file, struct io_uring *ring, int write, int sqthread,
		     int fixed, int buf_select)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int open_flags;
	int i, fd, ret;
	off_t offset;

	if (buf_select && write)
		write = 0;
	if (buf_select && fixed)
		fixed = 0;

	if (buf_select && provide_buffers(ring))
		return 1;

	if (write)
		open_flags = O_WRONLY;
	else
		open_flags = O_RDONLY;
	open_flags |= O_DIRECT;

	fd = open(file, open_flags);
	if (fd < 0) {
		perror("file open");
		goto err;
	}

	if (fixed) {
		ret = io_uring_register_buffers(ring, vecs, BUFFERS);
		if (ret) {
			fprintf(stderr, "buffer reg failed: %d\n", ret);
			goto err;
		}
	}
	if (sqthread) {
		ret = io_uring_register_files(ring, &fd, 1);
		if (ret) {
			fprintf(stderr, "file reg failed: %d\n", ret);
			goto err;
		}
	}

	offset = 0;
	for (i = 0; i < BUFFERS; i++) {
		sqe = io_uring_get_sqe(ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		offset = BS * (rand() % BUFFERS);
		if (write) {
			int do_fixed = fixed;
			int use_fd = fd;

			if (sqthread)
				use_fd = 0;
			if (fixed && (i & 1))
				do_fixed = 0;
			if (do_fixed) {
				io_uring_prep_write_fixed(sqe, use_fd, vecs[i].iov_base,
								vecs[i].iov_len,
								offset, i);
			} else {
				io_uring_prep_writev(sqe, use_fd, &vecs[i], 1,
								offset);
			}
		} else {
			int do_fixed = fixed;
			int use_fd = fd;

			if (sqthread)
				use_fd = 0;
			if (fixed && (i & 1))
				do_fixed = 0;
			if (do_fixed) {
				io_uring_prep_read_fixed(sqe, use_fd, vecs[i].iov_base,
								vecs[i].iov_len,
								offset, i);
			} else {
				io_uring_prep_readv(sqe, use_fd, &vecs[i], 1,
								offset);
			}

		}
		if (sqthread)
			sqe->flags |= IOSQE_FIXED_FILE;
		if (buf_select) {
			sqe->flags |= IOSQE_BUFFER_SELECT;
			sqe->buf_group = buf_select;
			sqe->user_data = i;
		}
	}

	ret = io_uring_submit(ring);
	if (ret != BUFFERS) {
		fprintf(stderr, "submit got %d, wanted %d\n", ret, BUFFERS);
		goto err;
	}

	for (i = 0; i < BUFFERS; i++) {
		ret = io_uring_wait_cqe(ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		} else if (cqe->res == -EOPNOTSUPP) {
			fprintf(stdout, "File/device/fs doesn't support polled IO\n");
			no_iopoll = 1;
			break;
		} else if (cqe->res != BS) {
			fprintf(stderr, "cqe res %d, wanted %d\n", cqe->res, BS);
			goto err;
		}
		io_uring_cqe_seen(ring, cqe);
	}

	if (fixed) {
		ret = io_uring_unregister_buffers(ring);
		if (ret) {
			fprintf(stderr, "buffer unreg failed: %d\n", ret);
			goto err;
		}
	}
	if (sqthread) {
		ret = io_uring_unregister_files(ring);
		if (ret) {
			fprintf(stderr, "file unreg failed: %d\n", ret);
			goto err;
		}
	}

	close(fd);
#ifdef VERBOSE
	fprintf(stdout, "PASS\n");
#endif
	return 0;
err:
#ifdef VERBOSE
	fprintf(stderr, "FAILED\n");
#endif
	if (fd != -1)
		close(fd);
	return 1;
}

static int test_io(const char *file, int write, int sqthread, int fixed,
		   int buf_select)
{
	struct io_uring ring;
	int ret, ring_flags;

	ring_flags = IORING_SETUP_IOPOLL;
	if (sqthread)
		ring_flags |= IORING_SETUP_SQPOLL;

	ret = io_uring_queue_init(64, &ring, ring_flags);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		return 1;
	}

	ret = __test_io(file, &ring, write, sqthread, fixed, buf_select);

	io_uring_queue_exit(&ring);
	return ret;
}

static int probe_buf_select(void)
{
	struct io_uring_probe *p;
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		return 1;
	}

	p = io_uring_get_probe_ring(&ring);
	if (!p || !io_uring_opcode_supported(p, IORING_OP_PROVIDE_BUFFERS)) {
		no_buf_select = 1;
		fprintf(stdout, "Buffer select not supported, skipping\n");
		return 0;
	}
	free(p);
	return 0;
}

int main(int argc, char *argv[])
{
	int i, ret, nr;
	char *fname;

	if (geteuid()) {
		fprintf(stdout, "iopoll requires root, skipping\n");
		return 0;
	}

	if (probe_buf_select())
		return 1;

	if (argc > 1) {
		fname = argv[1];
	} else {
		fname = ".iopoll-rw";
		if (create_file(".iopoll-rw")) {
			fprintf(stderr, "file creation failed\n");
			goto err;
		}
	}

	if (create_buffers()) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}

	nr = 16;
	if (no_buf_select)
		nr = 8;
	for (i = 0; i < nr; i++) {
		int v1, v2, v3, v4;

		v1 = (i & 1) != 0;
		v2 = (i & 2) != 0;
		v3 = (i & 4) != 0;
		v4 = (i & 8) != 0;
		ret = test_io(fname, v1, v2, v3, v4);
		if (ret) {
			fprintf(stderr, "test_io failed %d/%d/%d/%d\n", v1, v2, v3, v4);
			goto err;
		}
		if (no_iopoll)
			break;
	}

	if (fname != argv[1])
		unlink(fname);
	return 0;
err:
	if (fname != argv[1])
		unlink(fname);
	return 1;
}
