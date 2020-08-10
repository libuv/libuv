#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>

#include "liburing.h"

#define DIE(...) do {\
		fprintf(stderr, __VA_ARGS__);\
		abort();\
	} while(0);

static const int RSIZE = 2;
static const int OPEN_FLAGS = O_RDWR | O_CREAT;
static const mode_t OPEN_MODE = S_IRUSR | S_IWUSR;

static int open_io_uring(struct io_uring *ring, int dfd, const char *fn)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int ret, fd;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "failed to get sqe\n");
		return 1;
	}
	io_uring_prep_openat(sqe, dfd, fn, OPEN_FLAGS, OPEN_MODE);

	ret = io_uring_submit(ring);
	if (ret < 0) {
		fprintf(stderr, "failed to submit openat: %s\n", strerror(-ret));
		return 1;
	}

	ret = io_uring_wait_cqe(ring, &cqe);
	fd = cqe->res;
	io_uring_cqe_seen(ring, cqe);
	if (ret < 0) {
		fprintf(stderr, "wait_cqe failed: %s\n", strerror(-ret));
		return 1;
	} else if (fd < 0) {
		fprintf(stderr, "io_uring openat failed: %s\n", strerror(-fd));
		return 1;
	}

	close(fd);
	return 0;
}

static int prepare_file(int dfd, const char* fn)
{
	const char buf[] = "foo";
	int fd, res;

	fd = openat(dfd, fn, OPEN_FLAGS, OPEN_MODE);
	if (fd < 0) {
		fprintf(stderr, "prepare/open: %s\n", strerror(errno));
		return -1;
	}

	res = pwrite(fd, buf, sizeof(buf), 1ull << 32);
	if (res < 0)
		fprintf(stderr, "prepare/pwrite: %s\n", strerror(errno));

	close(fd);
	return res < 0 ? res : 0;
}

int main(int argc, char *argv[])
{
	const char *fn = "io_uring_openat_test";
	int dfd = open("/tmp", O_PATH);
	struct io_uring ring;
	int ret;

	if (argc > 1)
		return 0;

	if (dfd < 0)
		DIE("open /tmp: %s\n", strerror(errno));

	ret = io_uring_queue_init(RSIZE, &ring, 0);
	if (ret < 0)
		DIE("failed to init io_uring: %s\n", strerror(-ret));

	if (prepare_file(dfd, fn))
		return 1;

	ret = open_io_uring(&ring, dfd, fn);

	io_uring_queue_exit(&ring);
	close(dfd);
	unlink("/tmp/io_uring_openat_test");
	return ret;
}
