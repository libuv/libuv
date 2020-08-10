/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <poll.h>

#include "liburing.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static int recv_thread_ready = 0;
static int recv_thread_done = 0;

static void signal_var(int *var)
{
        pthread_mutex_lock(&mutex);
        *var = 1;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
}

static void wait_for_var(int *var)
{
        pthread_mutex_lock(&mutex);

        while (!*var)
                pthread_cond_wait(&cond, &mutex);

        pthread_mutex_unlock(&mutex);
}

struct data {
	unsigned expected[2];
	unsigned is_mask[2];
	unsigned long timeout;
	int port;
	int stop;
};

static void *send_thread(void *arg)
{
	struct data *data = arg;

	wait_for_var(&recv_thread_ready);

	int s0 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	assert(s0 != -1);

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = data->port;
	addr.sin_addr.s_addr = 0x0100007fU;

	if (connect(s0, (struct sockaddr*)&addr, sizeof(addr)) != -1)
		wait_for_var(&recv_thread_done);

	close(s0);
	return 0;
}

void *recv_thread(void *arg)
{
	struct data *data = arg;
	struct io_uring_sqe *sqe;
	struct io_uring ring;
	int i;

	assert(io_uring_queue_init(8, &ring, 0) == 0);

	int s0 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	assert(s0 != -1);

	int32_t val = 1;
        assert(setsockopt(s0, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) != -1);
        assert(setsockopt(s0, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != -1);

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = 0x0100007fU;

	i = 0;
	do {
		data->port = 1025 + (rand() % 64510);
		addr.sin_port = data->port;

		if (bind(s0, (struct sockaddr*)&addr, sizeof(addr)) != -1)
			break;
	} while (++i < 100);

	if (i >= 100) {
		fprintf(stderr, "Can't find good port, skipped\n");
		data->stop = 1;
		signal_var(&recv_thread_ready);
		goto out;
	}

        assert(listen(s0, 128) != -1);

	signal_var(&recv_thread_ready);

	sqe = io_uring_get_sqe(&ring);
	assert(sqe != NULL);

	io_uring_prep_poll_add(sqe, s0, POLLIN | POLLHUP | POLLERR);
	sqe->flags |= IOSQE_IO_LINK;
	sqe->user_data = 1;

	sqe = io_uring_get_sqe(&ring);
	assert(sqe != NULL);

	struct __kernel_timespec ts;
	ts.tv_sec = data->timeout / 1000000000;
	ts.tv_nsec = data->timeout % 1000000000;
	io_uring_prep_link_timeout(sqe, &ts, 0);
	sqe->user_data = 2;

	assert(io_uring_submit(&ring) == 2);

	for (i = 0; i < 2; i++) {
		struct io_uring_cqe *cqe;
		int idx;

		if (io_uring_wait_cqe(&ring, &cqe)) {
			fprintf(stderr, "wait cqe failed\n");
			goto err;
		}
		idx = cqe->user_data - 1;
		if (data->is_mask[idx] && !(data->expected[idx] & cqe->res)) {
			fprintf(stderr, "cqe %llu got %x, wanted mask %x\n",
					cqe->user_data, cqe->res,
					data->expected[idx]);
			goto err;
		} else if (!data->is_mask[idx] && cqe->res != data->expected[idx]) {
			fprintf(stderr, "cqe %llu got %d, wanted %d\n",
					cqe->user_data, cqe->res,
					data->expected[idx]);
			goto err;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

out:
	signal_var(&recv_thread_done);
	close(s0);
	io_uring_queue_exit(&ring);
	return NULL;
err:
	signal_var(&recv_thread_done);
	close(s0);
	io_uring_queue_exit(&ring);
	return (void *) 1;
}

static int test_poll_timeout(int do_connect, unsigned long timeout)
{
	pthread_t t1, t2;
	struct data d;
	void *tret;
	int ret = 0;

	recv_thread_ready = 0;
	recv_thread_done = 0;

	memset(&d, 0, sizeof(d));
	d.timeout = timeout;
	if (!do_connect) {
		d.expected[0] = -ECANCELED;
		d.expected[1] = -ETIME;
	} else {
		d.expected[0] = POLLIN;
		d.is_mask[0] = 1;
		d.expected[1] = -ECANCELED;
	}

	pthread_create(&t1, NULL, recv_thread, &d);

	if (do_connect)
		pthread_create(&t2, NULL, send_thread, &d);

	pthread_join(t1, &tret);
	if (tret)
		ret++;

	if (do_connect) {
		pthread_join(t2, &tret);
		if (tret)
			ret++;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	if (argc > 1)
		return 0;

	srand(getpid());

	if (test_poll_timeout(0, 200000000)) {
		fprintf(stderr, "poll timeout 0 failed\n");
		return 1;
	}

	if (test_poll_timeout(1, 1000000000)) {
		fprintf(stderr, "poll timeout 1 failed\n");
		return 1;
	}

	return 0;
}
