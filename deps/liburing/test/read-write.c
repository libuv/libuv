/* SPDX-License-Identifier: MIT */
/*
 * Description: basic read/write tests with buffered, O_DIRECT, and SQPOLL
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
static int no_read;
static int no_buf_select;
static int warned;

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

static int __test_io(const char *file, struct io_uring *ring, int write, int buffered,
		     int sqthread, int fixed, int mixed_fixed, int nonvec,
		     int buf_select, int seq, int exp_len)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int open_flags;
	int i, fd, ret;
	off_t offset;

#ifdef VERBOSE
	fprintf(stdout, "%s: start %d/%d/%d/%d/%d/%d: ", __FUNCTION__, write,
							buffered, sqthread,
							fixed, mixed_fixed,
							nonvec);
#endif
	if (sqthread && geteuid()) {
#ifdef VERBOSE
		fprintf(stdout, "SKIPPED (not root)\n");
#endif
		return 0;
	}

	if (write)
		open_flags = O_WRONLY;
	else
		open_flags = O_RDONLY;
	if (!buffered)
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
		if (!seq)
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
			} else if (nonvec) {
				io_uring_prep_write(sqe, use_fd, vecs[i].iov_base,
							vecs[i].iov_len, offset);
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
			} else if (nonvec) {
				io_uring_prep_read(sqe, use_fd, vecs[i].iov_base,
							vecs[i].iov_len, offset);
			} else {
				io_uring_prep_readv(sqe, use_fd, &vecs[i], 1,
								offset);
			}

		}
		if (sqthread)
			sqe->flags |= IOSQE_FIXED_FILE;
		if (buf_select) {
			if (nonvec)
				sqe->addr = 0;
			sqe->flags |= IOSQE_BUFFER_SELECT;
			sqe->buf_group = buf_select;
			sqe->user_data = i;
		}
		if (seq)
			offset += BS;
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
		}
		if (cqe->res == -EINVAL && nonvec) {
			if (!warned) {
				fprintf(stdout, "Non-vectored IO not "
					"supported, skipping\n");
				warned = 1;
				no_read = 1;
			}
		} else if (cqe->res != exp_len) {
			fprintf(stderr, "cqe res %d, wanted %d\n", cqe->res, exp_len);
			goto err;
		}
		if (buf_select && exp_len == BS) {
			int bid = cqe->flags >> 16;
			unsigned char *ptr = vecs[bid].iov_base;
			int j;

			for (j = 0; j < BS; j++) {
				if (ptr[j] == cqe->user_data)
					continue;

				fprintf(stderr, "Data mismatch! bid=%d, "
						"wanted=%d, got=%d\n", bid,
						(int)cqe->user_data, ptr[j]);
				return 1;
			}
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
static int test_io(const char *file, int write, int buffered, int sqthread,
		   int fixed, int mixed_fixed, int nonvec)
{
	struct io_uring ring;
	int ret, ring_flags;

	if (sqthread) {
		if (geteuid()) {
			if (!warned) {
				fprintf(stderr, "SQPOLL requires root, skipping\n");
				warned = 1;
			}
			return 0;
		}
		ring_flags = IORING_SETUP_SQPOLL;
	} else {
		ring_flags = 0;
	}

	ret = io_uring_queue_init(64, &ring, ring_flags);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		return 1;
	}

	ret = __test_io(file, &ring, write, buffered, sqthread, fixed,
			mixed_fixed, nonvec, 0, 0, BS);

	io_uring_queue_exit(&ring);
	return ret;
}

static int read_poll_link(const char *file)
{
	struct __kernel_timespec ts;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	int i, fd, ret, fds[2];

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret)
		return ret;

	fd = open(file, O_WRONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (pipe(fds)) {
		perror("pipe");
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_writev(sqe, fd, &vecs[0], 1, 0);
	sqe->flags |= IOSQE_IO_LINK;
	sqe->user_data = 1;

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_poll_add(sqe, fds[0], POLLIN);
	sqe->flags |= IOSQE_IO_LINK;
	sqe->user_data = 2;

	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_link_timeout(sqe, &ts, 0);
	sqe->user_data = 3;

	ret = io_uring_submit(&ring);
	if (ret != 3) {
		fprintf(stderr, "submitted %d\n", ret);
		return 1;
	}

	for (i = 0; i < 3; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			return 1;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	return 0;
}

static int has_nonvec_read(void)
{
	struct io_uring_probe *p;
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		exit(ret);
	}

	p = calloc(1, sizeof(*p) + 256 * sizeof(struct io_uring_probe_op));
	ret = io_uring_register_probe(&ring, p, 256);
	/* if we don't have PROBE_REGISTER, we don't have OP_READ/WRITE */
	if (ret == -EINVAL) {
out:
		io_uring_queue_exit(&ring);
		return 0;
	} else if (ret) {
		fprintf(stderr, "register_probe: %d\n", ret);
		goto out;
	}

	if (p->ops_len <= IORING_OP_READ)
		goto out;
	if (!(p->ops[IORING_OP_READ].flags & IO_URING_OP_SUPPORTED))
		goto out;
	io_uring_queue_exit(&ring);
	return 1;
}

static int test_eventfd_read(void)
{
	struct io_uring ring;
	int fd, ret;
	eventfd_t event;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;

	if (no_read)
		return 0;
	ret = io_uring_queue_init(8, &ring, 0);
	if (ret)
		return ret;

	fd = eventfd(1, 0);
	if (fd < 0) {
		perror("eventfd");
		return 1;
	}
	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_read(sqe, fd, &event, sizeof(eventfd_t), 0);
	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "submitted %d\n", ret);
		return 1;
	}
	eventfd_write(fd, 1);
	ret = io_uring_wait_cqe(&ring, &cqe);
	if (ret) {
		fprintf(stderr, "wait_cqe=%d\n", ret);
		return 1;
	}
	if (cqe->res == -EINVAL) {
		fprintf(stdout, "eventfd IO not supported, skipping\n");
	} else if (cqe->res != sizeof(eventfd_t)) {
		fprintf(stderr, "cqe res %d, wanted %d\n", cqe->res,
						(int) sizeof(eventfd_t));
		return 1;
	}
	io_uring_cqe_seen(&ring, cqe);
	return 0;
}

static int test_buf_select_short(const char *filename, int nonvec)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	int ret, i, exp_len;

	if (no_buf_select)
		return 0;

	ret = io_uring_queue_init(64, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		return 1;
	}

	exp_len = 0;
	for (i = 0; i < BUFFERS; i++) {
		sqe = io_uring_get_sqe(&ring);
		io_uring_prep_provide_buffers(sqe, vecs[i].iov_base,
						vecs[i].iov_len / 2, 1, 1, i);
		if (!exp_len)
			exp_len = vecs[i].iov_len / 2;
	}

	ret = io_uring_submit(&ring);
	if (ret != BUFFERS) {
		fprintf(stderr, "submit: %d\n", ret);
		return -1;
	}

	for (i = 0; i < BUFFERS; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (cqe->res < 0) {
			fprintf(stderr, "cqe->res=%d\n", cqe->res);
			return 1;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	ret = __test_io(filename, &ring, 0, 0, 0, 0, 0, nonvec, 1, 1, exp_len);

	io_uring_queue_exit(&ring);
	return ret;
}

static int test_buf_select(const char *filename, int nonvec)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring_probe *p;
	struct io_uring ring;
	int ret, i;

	ret = io_uring_queue_init(64, &ring, 0);
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

	/*
	 * Write out data with known pattern
	 */
	for (i = 0; i < BUFFERS; i++)
		memset(vecs[i].iov_base, i, vecs[i].iov_len);

	ret = __test_io(filename, &ring, 1, 0, 0, 0, 0, 0, 0, 1, BS);
	if (ret) {
		fprintf(stderr, "failed writing data\n");
		return 1;
	}

	for (i = 0; i < BUFFERS; i++)
		memset(vecs[i].iov_base, 0x55, vecs[i].iov_len);

	for (i = 0; i < BUFFERS; i++) {
		sqe = io_uring_get_sqe(&ring);
		io_uring_prep_provide_buffers(sqe, vecs[i].iov_base,
						vecs[i].iov_len, 1, 1, i);
	}

	ret = io_uring_submit(&ring);
	if (ret != BUFFERS) {
		fprintf(stderr, "submit: %d\n", ret);
		return -1;
	}

	for (i = 0; i < BUFFERS; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (cqe->res < 0) {
			fprintf(stderr, "cqe->res=%d\n", cqe->res);
			return 1;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	ret = __test_io(filename, &ring, 0, 0, 0, 0, 0, nonvec, 1, 1, BS);

	io_uring_queue_exit(&ring);
	return ret;
}

static int test_io_link(const char *file)
{
	const int nr_links = 100;
	const int link_len = 100;
	const int nr_sqes = nr_links * link_len;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	int i, j, fd, ret;

	fd = open(file, O_WRONLY);
	if (fd < 0) {
		perror("file open");
		goto err;
	}

	ret = io_uring_queue_init(nr_sqes, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		goto err;
	}

	for (i = 0; i < nr_links; ++i) {
		for (j = 0; j < link_len; ++j) {
			sqe = io_uring_get_sqe(&ring);
			if (!sqe) {
				fprintf(stderr, "sqe get failed\n");
				goto err;
			}
			io_uring_prep_writev(sqe, fd, &vecs[0], 1, 0);
			sqe->flags |= IOSQE_ASYNC;
			if (j != link_len - 1)
				sqe->flags |= IOSQE_IO_LINK;
		}
	}

	ret = io_uring_submit(&ring);
	if (ret != nr_sqes) {
		ret = io_uring_peek_cqe(&ring, &cqe);
		if (!ret && cqe->res == -EINVAL) {
			fprintf(stdout, "IOSQE_ASYNC not supported, skipped\n");
			goto out;
		}
		fprintf(stderr, "submit got %d, wanted %d\n", ret, nr_sqes);
		goto err;
	}

	for (i = 0; i < nr_sqes; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		}
		if (cqe->res == -EINVAL) {
			if (!warned) {
				fprintf(stdout, "Non-vectored IO not "
					"supported, skipping\n");
				warned = 1;
				no_read = 1;
			}
		} else if (cqe->res != BS) {
			fprintf(stderr, "cqe res %d, wanted %d\n", cqe->res, BS);
			goto err;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

out:
	io_uring_queue_exit(&ring);
	close(fd);
	return 0;
err:
	if (fd != -1)
		close(fd);
	return 1;
}

static int test_write_efbig(void)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	struct rlimit rlim;
	int i, fd, ret;
	loff_t off;

	if (getrlimit(RLIMIT_FSIZE, &rlim) < 0) {
		perror("getrlimit");
		return 1;
	}
	rlim.rlim_cur = 64 * 1024;
	rlim.rlim_max = 64 * 1024;
	if (setrlimit(RLIMIT_FSIZE, &rlim) < 0) {
		perror("setrlimit");
		return 1;
	}

	fd = open(".efbig", O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror("file open");
		goto err;
	}

	ret = io_uring_queue_init(32, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring create failed: %d\n", ret);
		goto err;
	}

	off = 0;
	for (i = 0; i < 32; i++) {
		sqe = io_uring_get_sqe(&ring);
		if (!sqe) {
			fprintf(stderr, "sqe get failed\n");
			goto err;
		}
		io_uring_prep_writev(sqe, fd, &vecs[i], 1, off);
		off += BS;
	}

	ret = io_uring_submit(&ring);
	if (ret != 32) {
		fprintf(stderr, "submit got %d, wanted %d\n", ret, 32);
		goto err;
	}

	for (i = 0; i < 32; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		}
		if (i < 16) {
			if (cqe->res != BS) {
				fprintf(stderr, "bad write: %d\n", cqe->res);
				goto err;
			}
		} else {
			if (cqe->res != -EFBIG) {
				fprintf(stderr, "Expected -EFBIG: %d\n", cqe->res);
				goto err;
			}
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	io_uring_queue_exit(&ring);
	close(fd);
	unlink(".efbig");
	return 0;
err:
	if (fd != -1)
		close(fd);
	unlink(".efbig");
	return 1;
}

int main(int argc, char *argv[])
{
	int i, ret, nr;
	char *fname;

	if (argc > 1) {
		fname = argv[1];
	} else {
		fname = ".basic-rw";
		if (create_file(fname)) {
			fprintf(stderr, "file creation failed\n");
			goto err;
		}
	}

	if (create_buffers()) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}

	/* if we don't have nonvec read, skip testing that */
	if (has_nonvec_read())
		nr = 64;
	else
		nr = 32;

	for (i = 0; i < nr; i++) {
		int v1, v2, v3, v4, v5, v6;

		v1 = (i & 1) != 0;
		v2 = (i & 2) != 0;
		v3 = (i & 4) != 0;
		v4 = (i & 8) != 0;
		v5 = (i & 16) != 0;
		v6 = (i & 32) != 0;
		ret = test_io(fname, v1, v2, v3, v4, v5, v6);
		if (ret) {
			fprintf(stderr, "test_io failed %d/%d/%d/%d/%d/%d\n",
					v1, v2, v3, v4, v5, v6);
			goto err;
		}
	}

	ret = test_buf_select(fname, 1);
	if (ret) {
		fprintf(stderr, "test_buf_select nonvec failed\n");
		goto err;
	}

	ret = test_buf_select(fname, 0);
	if (ret) {
		fprintf(stderr, "test_buf_select vec failed\n");
		goto err;
	}

	ret = test_buf_select_short(fname, 1);
	if (ret) {
		fprintf(stderr, "test_buf_select_short nonvec failed\n");
		goto err;
	}

	ret = test_buf_select_short(fname, 0);
	if (ret) {
		fprintf(stderr, "test_buf_select_short vec failed\n");
		goto err;
	}

	ret = test_eventfd_read();
	if (ret) {
		fprintf(stderr, "test_eventfd_read failed\n");
		goto err;
	}

	ret = read_poll_link(fname);
	if (ret) {
		fprintf(stderr, "read_poll_link failed\n");
		goto err;
	}

	ret = test_io_link(fname);
	if (ret) {
		fprintf(stderr, "test_io_link failed\n");
		goto err;
	}

	ret = test_write_efbig();
	if (ret) {
		fprintf(stderr, "test_write_efbig failed\n");
		goto err;
	}

	if (fname != argv[1])
		unlink(fname);
	return 0;
err:
	if (fname != argv[1])
		unlink(fname);
	return 1;
}
