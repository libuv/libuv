/* SPDX-License-Identifier: MIT */
/*
 * Test case for socket read/write through IORING_OP_READV and
 * IORING_OP_WRITEV, using both TCP and sockets and blocking and
 * non-blocking IO.
 *
 * Heavily based on a test case from Hrvoje Zeba <zeba.hrvoje@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include "liburing.h"

#define RECV_BUFF_SIZE 2
#define SEND_BUFF_SIZE 3

#define PORT	0x1235

struct params {
	int tcp;
	int non_blocking;
};

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int rcv_ready = 0;

static void set_rcv_ready(void)
{
	pthread_mutex_lock(&mutex);

	rcv_ready = 1;
	pthread_cond_signal(&cond);

	pthread_mutex_unlock(&mutex);
}

static void wait_for_rcv_ready(void)
{
	pthread_mutex_lock(&mutex);

	while (!rcv_ready)
		pthread_cond_wait(&cond, &mutex);

	pthread_mutex_unlock(&mutex);
}

static void *rcv(void *arg)
{
	struct params *p = arg;
	int s0;

	if (p->tcp) {
		int val = 1;

		s0 = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
		assert(setsockopt(s0, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) != -1);
		assert(setsockopt(s0, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != -1);

		struct sockaddr_in addr;

		addr.sin_family = AF_INET;
		addr.sin_port = PORT;
		addr.sin_addr.s_addr = 0x0100007fU;
		assert(bind(s0, (struct sockaddr *) &addr, sizeof(addr)) != -1);
	} else {
		s0 = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		assert(s0 != -1);

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));

		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, "\0sock", 6);
		assert(bind(s0, (struct sockaddr *) &addr, sizeof(addr)) != -1);
	}

	assert(listen(s0, 128) != -1);

	set_rcv_ready();

	int s1 = accept(s0, NULL, NULL);
	assert(s1 != -1);

	if (p->non_blocking) {
		int flags = fcntl(s1, F_GETFL, 0);
		assert(flags != -1);

		flags |= O_NONBLOCK;
		assert(fcntl(s1, F_SETFL, flags) != -1);
	}

	struct io_uring m_io_uring;
	void *ret = NULL;

	assert(io_uring_queue_init(32, &m_io_uring, 0) >= 0);

	int bytes_read = 0;
	int expected_byte = 0;
	int done = 0;

	while (!done && bytes_read != 33) {
		char buff[RECV_BUFF_SIZE];
		struct iovec iov;

		iov.iov_base = buff;
		iov.iov_len = sizeof(buff);

		struct io_uring_sqe *sqe = io_uring_get_sqe(&m_io_uring);
		assert(sqe != NULL);

		io_uring_prep_readv(sqe, s1, &iov, 1, 0);

		assert(io_uring_submit(&m_io_uring) != -1);

		struct io_uring_cqe *cqe;
		unsigned head;
		unsigned count = 0;

		while (!done && count != 1) {
			io_uring_for_each_cqe(&m_io_uring, head, cqe) {
				if (cqe->res < 0)
					assert(cqe->res == -EAGAIN);
				else {
					int i;

					for (i = 0; i < cqe->res; i++) {
						if (buff[i] != expected_byte) {
							fprintf(stderr,
								"Received %d, wanted %d\n",
								buff[i], expected_byte);
							ret++;
							done = 1;
						 }
						 expected_byte++;
					}
					bytes_read += cqe->res;
				}

				count++;
			}

			assert(count <= 1);
			io_uring_cq_advance(&m_io_uring, count);
		}
	}

	shutdown(s1, SHUT_RDWR);
	close(s1);
	close(s0);
	io_uring_queue_exit(&m_io_uring);
	return ret;
}

static void *snd(void *arg)
{
	struct params *p = arg;
	int s0;

	wait_for_rcv_ready();

	if (p->tcp) {
		int val = 1;

		s0 = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
		assert(setsockopt(s0, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) != -1);

		struct sockaddr_in addr;

		addr.sin_family = AF_INET;
		addr.sin_port = PORT;
		addr.sin_addr.s_addr = 0x0100007fU;
		assert(connect(s0, (struct sockaddr*) &addr, sizeof(addr)) != -1);
	} else {
		s0 = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		assert(s0 != -1);

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));

		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, "\0sock", 6);
		assert(connect(s0, (struct sockaddr*) &addr, sizeof(addr)) != -1);
	}

	if (p->non_blocking) {
		int flags = fcntl(s0, F_GETFL, 0);
		assert(flags != -1);

		flags |= O_NONBLOCK;
		assert(fcntl(s0, F_SETFL, flags) != -1);
	}

	struct io_uring m_io_uring;

	assert(io_uring_queue_init(32, &m_io_uring, 0) >= 0);

	int bytes_written = 0;
	int done = 0;

	while (!done && bytes_written != 33) {
		char buff[SEND_BUFF_SIZE];
		int i;

		for (i = 0; i < SEND_BUFF_SIZE; i++)
			buff[i] = i + bytes_written;

		struct iovec iov;

		iov.iov_base = buff;
		iov.iov_len = sizeof(buff);

		struct io_uring_sqe *sqe = io_uring_get_sqe(&m_io_uring);
		assert(sqe != NULL);

		io_uring_prep_writev(sqe, s0, &iov, 1, 0);

		assert(io_uring_submit(&m_io_uring) != -1);

		struct io_uring_cqe *cqe;
		unsigned head;
		unsigned count = 0;

		while (!done && count != 1) {
			io_uring_for_each_cqe(&m_io_uring, head, cqe) {
				if (cqe->res < 0) {
					if (cqe->res == -EPIPE) {
						done = 1;
						break;
					}
					assert(cqe->res == -EAGAIN);
				} else {
					bytes_written += cqe->res;
				}

				count++;
			}

			assert(count <= 1);
			io_uring_cq_advance(&m_io_uring, count);
		}
		usleep(100000);
	}

	shutdown(s0, SHUT_RDWR);
	close(s0);
	io_uring_queue_exit(&m_io_uring);
	return NULL;
}

int main(int argc, char *argv[])
{
	struct params p;
	pthread_t t1, t2;
	void *res1, *res2;
	int i, exit_val = 0;

	if (argc > 1)
		return 0;

	for (i = 0; i < 4; i++) {
		p.tcp = i & 1;
		p.non_blocking = (i & 2) >> 1;

		rcv_ready = 0;

		pthread_create(&t1, NULL, rcv, &p);
		pthread_create(&t2, NULL, snd, &p);
		pthread_join(t1, &res1);
		pthread_join(t2, &res2);
		if (res1 || res2) {
			fprintf(stderr, "Failed tcp=%d, non_blocking=%d\n", p.tcp, p.non_blocking);
			exit_val = 1;
		}
	}

	return exit_val;
}
