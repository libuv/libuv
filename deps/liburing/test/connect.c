/* SPDX-License-Identifier: MIT */
/*
 * Check that IORING_OP_CONNECT works, with and without other side
 * being open.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "liburing.h"

static int create_socket(void)
{
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == -1) {
		perror("socket()");
		return -1;
	}

	return fd;
}

static int submit_and_wait(struct io_uring *ring, int *res)
{
	struct io_uring_cqe *cqe;
	int ret;

	ret = io_uring_submit_and_wait(ring, 1);
	if (ret != 1) {
		fprintf(stderr, "io_using_submit: got %d\n", ret);
		return 1;
	}

	ret = io_uring_peek_cqe(ring, &cqe);
	if (ret) {
		fprintf(stderr, "io_uring_peek_cqe(): no cqe returned");
		return 1;
	}

	*res = cqe->res;
	io_uring_cqe_seen(ring, cqe);
	return 0;
}

static int wait_for(struct io_uring *ring, int fd, int mask)
{
	struct io_uring_sqe *sqe;
	int ret, res;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "unable to get sqe\n");
		return -1;
	}

	io_uring_prep_poll_add(sqe, fd, mask);
	sqe->user_data = 2;

	ret = submit_and_wait(ring, &res);
	if (ret)
		return -1;

	if (res < 0) {
		fprintf(stderr, "poll(): failed with %d\n", res);
		return -1;
	}

	return res;
}

static int listen_on_socket(int fd)
{
	struct sockaddr_in addr;
	int ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 0x1234;
	addr.sin_addr.s_addr = 0x0100007fU;

	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		perror("bind()");
		return -1;
	}

	ret = listen(fd, 128);
	if (ret == -1) {
		perror("listen()");
		return -1;
	}

	return 0;
}

static int connect_socket(struct io_uring *ring, int fd, int *code)
{
	struct io_uring_sqe *sqe;
	struct sockaddr_in addr;
	int ret, res, val = 1;
	socklen_t code_len = sizeof(*code);

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
	if (ret == -1) {
		perror("setsockopt()");
		return -1;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if (ret == -1) {
		perror("setsockopt()");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 0x1234;
	addr.sin_addr.s_addr = 0x0100007fU;

	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "unable to get sqe\n");
		return -1;
	}

	io_uring_prep_connect(sqe, fd, (struct sockaddr*)&addr, sizeof(addr));
	sqe->user_data = 1;

	ret = submit_and_wait(ring, &res);
	if (ret)
		return -1;

	if (res == -EINPROGRESS) {
		ret = wait_for(ring, fd, POLLOUT | POLLHUP | POLLERR);
		if (ret == -1)
			return -1;

		int ev = (ret & POLLOUT) || (ret & POLLHUP) || (ret & POLLERR);
		if (!ev) {
			fprintf(stderr, "poll(): returned invalid value %#x\n", ret);
			return -1;
		}

		ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, code, &code_len);
		if (ret == -1) {
			perror("getsockopt()");
			return -1;
		}
	} else
		*code = res;
	return 0;
}

static int test_connect_with_no_peer(struct io_uring *ring)
{
	int connect_fd;
	int ret, code;

	connect_fd = create_socket();
	if (connect_fd == -1)
		return -1;

	ret = connect_socket(ring, connect_fd, &code);
	if (ret == -1)
		goto err;

	if (code != -ECONNREFUSED) {
		fprintf(stderr, "connect failed with %d\n", code);
		goto err;
	}

	close(connect_fd);
	return 0;

err:
	close(connect_fd);
	return -1;
}

static int test_connect(struct io_uring *ring)
{
	int accept_fd;
	int connect_fd;
	int ret, code;

	accept_fd = create_socket();
	if (accept_fd == -1)
		return -1;

	ret = listen_on_socket(accept_fd);
	if (ret == -1)
		goto err1;

	connect_fd = create_socket();
	if (connect_fd == -1)
		goto err1;

	ret = connect_socket(ring, connect_fd, &code);
	if (ret == -1)
		goto err2;

	if (code != 0) {
		fprintf(stderr, "connect failed with %d\n", code);
		goto err2;
	}

	close(connect_fd);
	close(accept_fd);

	return 0;

err2:
	close(connect_fd);

err1:
	close(accept_fd);
	return -1;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	int ret;

	if (argc > 1)
		return 0;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "io_uring_queue_setup() = %d\n", ret);
		return 1;
	}

	ret = test_connect_with_no_peer(&ring);
	if (ret == -1) {
		fprintf(stderr, "test_connect_with_no_peer(): failed\n");
		return 1;
	}

	ret = test_connect(&ring);
	if (ret == -1) {
		fprintf(stderr, "test_connect(): failed\n");
		return 1;
	}

	io_uring_queue_exit(&ring);
	return 0;
}
