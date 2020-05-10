#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "liburing.h"

static int no_splice = 0;

static int copy_single(struct io_uring *ring,
			int fd_in, loff_t off_in,
			int fd_out, loff_t off_out,
			int pipe_fds[2],
			unsigned int len,
			unsigned flags1, unsigned flags2)
{
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int i, ret = -1;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return -1;
	}
	io_uring_prep_splice(sqe, fd_in, off_in, pipe_fds[1], -1,
			     len, flags1);
	sqe->flags = IOSQE_IO_LINK;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return -1;
	}
	io_uring_prep_splice(sqe, pipe_fds[0], -1, fd_out, off_out,
			     len, flags2);

	ret = io_uring_submit(ring);
	if (ret < 2) {
		/* submitted just one, kernel likely doesn't support splice */
		if (!io_uring_peek_cqe(ring, &cqe) &&
		    cqe->res == -EINVAL) {
			no_splice = 1;
			return -1;
		}
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		return -1;
	}

	for (i = 0; i < 2; i++) {
		ret = io_uring_wait_cqe(ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "wait completion %d\n", cqe->res);
			return ret;
		}

		ret = cqe->res;
		if (ret != len) {
			fprintf(stderr, "splice: returned %i, expected %i\n",
				cqe->res, len);
			return ret < 0 ? ret : -1;
		}
		io_uring_cqe_seen(ring, cqe);
	}
	return 0;
}

static int test_splice(struct io_uring *ring)
{
	int ret = -1, len = 4 * 4096;
	int fd_out = -1, fd_in = -1;
	int pipe_fds[2] = {-1, -1};

	if (pipe(pipe_fds) < 0)
		goto exit;
	fd_in = open("/dev/urandom", O_RDONLY);
	if (fd_in < 0)
		goto exit;
	fd_out = open(".splice_fd_out", O_CREAT | O_WRONLY, 0644);
	if (fd_out < 0)
		goto exit;
	if (ftruncate(fd_out, len) == -1)
		goto exit;

	ret = copy_single(ring, fd_in, -1, fd_out, -1, pipe_fds,
			  len, SPLICE_F_MOVE | SPLICE_F_MORE, 0);
	if (ret == -EINVAL) {
		no_splice = 1;
		goto exit;
	}
	if (ret) {
		fprintf(stderr, "basic splice-copy failed\n");
		goto exit;
	}

	ret = copy_single(ring, fd_in, 0, fd_out, 0, pipe_fds,
			  len, 0, SPLICE_F_MOVE | SPLICE_F_MORE);
	if (ret) {
		fprintf(stderr, "basic splice with offset failed\n");
		goto exit;
	}

	ret = io_uring_register_files(ring, &fd_in, 1);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto exit;
	}

	ret = copy_single(ring, 0, 0, fd_out, 0, pipe_fds,
			  len, SPLICE_F_FD_IN_FIXED, 0);
	if (ret) {
		fprintf(stderr, "basic splice with reg files failed\n");
		goto exit;
	}

	ret = 0;
exit:
	if (fd_out >= 0) {
		unlink(".splice_fd_out");
		close(fd_out);
	}
	if (fd_in >= 0)
		close(fd_in);
	if (pipe_fds[0] >= 0) {
		close(pipe_fds[0]);
		close(pipe_fds[1]);
	}
	return ret;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed\n");
		return 1;
	}

	ret = test_splice(&ring);
	if (ret && no_splice) {
		fprintf(stdout, "skip, doesn't support splice()\n");
		return 0;
	}
	if (ret) {
		fprintf(stderr, "test_splice failed %i %i\n", ret, errno);
		return ret;
	}

	return 0;
}
