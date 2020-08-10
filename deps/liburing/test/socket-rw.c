/* SPDX-License-Identifier: MIT */
/*
 * Check that a readv on a socket queued before a writev doesn't hang
 * the processing.
 *
 * From Hrvoje Zeba <zeba.hrvoje@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include "liburing.h"

int main(int argc, char *argv[])
{
	int p_fd[2];
	int32_t recv_s0;
	int32_t val = 1;
	struct sockaddr_in addr;

	if (argc > 1)
		return 0;

	recv_s0 = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

	assert(setsockopt(recv_s0, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) != -1);
	assert(setsockopt(recv_s0, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != -1);

	addr.sin_family = AF_INET;
	addr.sin_port = 0x1235;
	addr.sin_addr.s_addr = 0x0100007fU;

	assert(bind(recv_s0, (struct sockaddr*)&addr, sizeof(addr)) != -1);
	assert(listen(recv_s0, 128) != -1);


	p_fd[1] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

	val = 1;
	assert(setsockopt(p_fd[1], IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) != -1);

	int32_t flags = fcntl(p_fd[1], F_GETFL, 0);
	assert(flags != -1);

	flags |= O_NONBLOCK;
	assert(fcntl(p_fd[1], F_SETFL, flags) != -1);

	assert(connect(p_fd[1], (struct sockaddr*)&addr, sizeof(addr)) == -1);

	flags = fcntl(p_fd[1], F_GETFL, 0);
	assert(flags != -1);

	flags &= ~O_NONBLOCK;
	assert(fcntl(p_fd[1], F_SETFL, flags) != -1);

	p_fd[0] = accept(recv_s0, NULL, NULL);
	assert(p_fd[0] != -1);

	while (1) {
		int32_t code;
		socklen_t code_len = sizeof(code);

		assert(getsockopt(p_fd[1], SOL_SOCKET, SO_ERROR, &code, &code_len) != -1);

		if (!code)
			break;
	}

	struct io_uring m_io_uring;

	assert(io_uring_queue_init(32, &m_io_uring, 0) >= 0);

	char recv_buff[128];
	char send_buff[128];

	{
		struct iovec iov[1];

		iov[0].iov_base = recv_buff;
		iov[0].iov_len = sizeof(recv_buff);

		struct io_uring_sqe* sqe = io_uring_get_sqe(&m_io_uring);
		assert(sqe != NULL);

		io_uring_prep_readv(sqe, p_fd[0], iov, 1, 0);
	}

	{
		struct iovec iov[1];

		iov[0].iov_base = send_buff;
		iov[0].iov_len = sizeof(send_buff);

		struct io_uring_sqe* sqe = io_uring_get_sqe(&m_io_uring);
		assert(sqe != NULL);

		io_uring_prep_writev(sqe, p_fd[1], iov, 1, 0);
	}

	assert(io_uring_submit_and_wait(&m_io_uring, 2) != -1);

	struct io_uring_cqe* cqe;
	uint32_t head;
	uint32_t count = 0;

	while (count != 2) {
		io_uring_for_each_cqe(&m_io_uring, head, cqe) {
			assert(cqe->res == 128);
			count++;
		}

		assert(count <= 2);
		io_uring_cq_advance(&m_io_uring, count);
	}

	io_uring_queue_exit(&m_io_uring);
	return 0;
}
