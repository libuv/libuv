/* SPDX-License-Identifier: MIT */
/*
 * Simple test case showing using send and recv through io_uring
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include "liburing.h"

static char str[] = "This is a test of send and recv over io_uring!";

#define MAX_MSG	128

#define PORT	10200
#define HOST	"127.0.0.1"

#if 0
#	define io_uring_prep_send io_uring_prep_write
#	define io_uring_prep_recv io_uring_prep_read
#endif

static int recv_prep(struct io_uring *ring, struct iovec *iov)
{
	struct sockaddr_in saddr;
	struct io_uring_sqe *sqe;
	int sockfd, ret;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(PORT);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return 1;
	}

	ret = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret < 0) {
		perror("bind");
		goto err;
	}

	sqe = io_uring_get_sqe(ring);
	io_uring_prep_recv(sqe, sockfd, iov->iov_base, iov->iov_len, 0);
	sqe->user_data = 2;

	ret = io_uring_submit(ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		goto err;
	}

	return 0;
err:
	close(sockfd);
	return 1;
}

static int do_recv(struct io_uring *ring, struct iovec *iov)
{
	struct io_uring_cqe *cqe;

	io_uring_wait_cqe(ring, &cqe);
	if (cqe->res == -EINVAL) {
		fprintf(stdout, "recv not supported, skipping\n");
		return 0;
	}
	if (cqe->res < 0) {
		fprintf(stderr, "failed cqe: %d\n", cqe->res);
		goto err;
	}

	if (cqe->res -1 != strlen(str)) {
		fprintf(stderr, "got wrong length: %d/%d\n", cqe->res,
							(int) strlen(str) + 1);
		goto err;
	}

	if (strcmp(str, iov->iov_base)) {
		fprintf(stderr, "string mismatch\n");
		goto err;
	}

	return 0;
err:
	return 1;
}

static void *recv_fn(void *data)
{
	pthread_mutex_t *mutex = data;
	char buf[MAX_MSG + 1];
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = sizeof(buf) - 1,
	};
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		goto err;
	}

	ret = recv_prep(&ring, &iov);
	if (ret) {
		fprintf(stderr, "recv_prep failed: %d\n", ret);
		goto err;
	}
	pthread_mutex_unlock(mutex);
	ret = do_recv(&ring, &iov);

	io_uring_queue_exit(&ring);

err:
	return (void *)(intptr_t)ret;
}

static int do_send(void)
{
	struct sockaddr_in saddr;
	struct iovec iov = {
		.iov_base = str,
		.iov_len = sizeof(str),
	};
	struct io_uring ring;
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	int sockfd, ret;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		return 1;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(PORT);
	inet_pton(AF_INET, HOST, &saddr.sin_addr);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return 1;
	}

	ret = connect(sockfd, &saddr, sizeof(saddr));
	if (ret < 0) {
		perror("connect");
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_send(sqe, sockfd, iov.iov_base, iov.iov_len, 0);
	sqe->user_data = 1;

	ret = io_uring_submit(&ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(&ring, &cqe);
	if (cqe->res == -EINVAL) {
		fprintf(stdout, "send not supported, skipping\n");
		close(sockfd);
		return 0;
	}
	if (cqe->res != iov.iov_len) {
		fprintf(stderr, "failed cqe: %d\n", cqe->res);
		goto err;
	}

	close(sockfd);
	return 0;
err:
	close(sockfd);
	return 1;
}

int main(int argc, char *argv[])
{
	pthread_mutexattr_t attr;
	pthread_t recv_thread;
	pthread_mutex_t mutex;
	int ret;
	void *retval;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, 1);
	pthread_mutex_init(&mutex, &attr);
	pthread_mutex_lock(&mutex);

	ret = pthread_create(&recv_thread, NULL, recv_fn, &mutex);
	if (ret) {
		fprintf(stderr, "Thread create failed: %d\n", ret);
		return 1;
	}

	pthread_mutex_lock(&mutex);
	do_send();
	pthread_join(recv_thread, &retval);
	ret = (int)(intptr_t)retval;

	return ret;
}
