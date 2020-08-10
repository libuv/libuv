/* SPDX-License-Identifier: MIT */
/*
 * Description: Check to see if accept handles addr and addrlen
 */
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include "liburing.h"

int main(int argc, char *argv[])
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	struct sockaddr_un addr;
	socklen_t addrlen = sizeof(addr);
	int ret, fd;
	struct __kernel_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 1000000
	};

	if (argc > 1)
		return 0;

	if (io_uring_queue_init(4, &ring, 0) != 0) {
		fprintf(stderr, "ring setup failed\n");
		return 1;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(fd != -1);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, "\0sock", 6);

	assert(bind(fd, (struct sockaddr *)&addr, addrlen) != -1);
	assert(listen(fd, 128) != -1);

	sqe = io_uring_get_sqe(&ring);
	if (!sqe) {
		fprintf(stderr, "get sqe failed\n");
		return 1;
	}
	io_uring_prep_accept(sqe, fd, (struct sockaddr*)&addr, &addrlen, 0);
	sqe->user_data = 1;

	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "Got submit %d, expected 1\n", ret);
		return 1;
	}

	ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
	if (ret != -ETIME) {
		fprintf(stderr, "accept() failed to use addr & addrlen parameters!\n");
		return 1;
	}

	io_uring_queue_exit(&ring);
	return 0;
}
