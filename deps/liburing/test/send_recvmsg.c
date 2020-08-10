/* SPDX-License-Identifier: MIT */
/*
 * Simple test case showing using sendmsg and recvmsg through io_uring
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include "liburing.h"

static char str[] = "This is a test of sendmsg and recvmsg over io_uring!";

#define MAX_MSG	128

#define PORT	10200
#define HOST	"127.0.0.1"

#define BUF_BGID	10
#define BUF_BID		89

static int recv_prep(struct io_uring *ring, struct iovec *iov, int bgid)
{
	struct sockaddr_in saddr;
	struct msghdr msg;
	struct io_uring_sqe *sqe;
	int sockfd, ret;
	int val = 1;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(PORT);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return 1;
	}

	val = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	ret = bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret < 0) {
		perror("bind");
		goto err;
	}

	memset(&msg, 0, sizeof(msg));
        msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	sqe = io_uring_get_sqe(ring);
	io_uring_prep_recvmsg(sqe, sockfd, &msg, 0);
	if (bgid) {
		sqe->user_data = (unsigned long) iov->iov_base;
		iov->iov_base = NULL;
		sqe->flags |= IOSQE_BUFFER_SELECT;
		sqe->buf_group = bgid;
	}

	ret = io_uring_submit(ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		goto err;
	}

	close(sockfd);
	return 0;
err:
	close(sockfd);
	return 1;
}

struct recv_data {
	pthread_mutex_t *mutex;
	int buf_select;
	int no_buf_add;
};

static int do_recvmsg(struct io_uring *ring, struct iovec *iov,
		      struct recv_data *rd)
{
	struct io_uring_cqe *cqe;
	int ret;

	ret = io_uring_wait_cqe(ring, &cqe);
	if (ret) {
		fprintf(stdout, "wait_cqe: %d\n", ret);
		goto err;
	}
	if (cqe->res < 0) {
		if (rd->no_buf_add && rd->buf_select)
			return 0;
		fprintf(stderr, "%s: failed cqe: %d\n", __FUNCTION__, cqe->res);
		goto err;
	}
	if (cqe->flags) {
		int bid = cqe->flags >> 16;
		if (bid != BUF_BID)
			fprintf(stderr, "Buffer ID mismatch %d\n", bid);
		/* just for passing the pointer to str */
		iov->iov_base = (void *) (uintptr_t) cqe->user_data;
	}

	if (rd->no_buf_add && rd->buf_select) {
		fprintf(stderr, "Expected -ENOBUFS: %d\n", cqe->res);
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
	struct recv_data *rd = data;
	pthread_mutex_t *mutex = rd->mutex;
	char buf[MAX_MSG + 1];
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = sizeof(buf) - 1,
	};
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		goto err;
	}

	if (rd->buf_select && !rd->no_buf_add) {
		sqe = io_uring_get_sqe(&ring);
		io_uring_prep_provide_buffers(sqe, buf, sizeof(buf) -1, 1,
						BUF_BGID, BUF_BID);
		ret = io_uring_submit(&ring);
		if (ret != 1) {
			fprintf(stderr, "submit ret=%d\n", ret);
			goto err;
		}

		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait_cqe=%d\n", ret);
			goto err;
		}
		ret = cqe->res;
		io_uring_cqe_seen(&ring, cqe);
		if (ret == -EINVAL) {
			fprintf(stdout, "PROVIDE_BUFFERS not supported, skip\n");
			goto out;
			goto err;
		} else if (ret < 0) {
			fprintf(stderr, "PROVIDER_BUFFERS %d\n", ret);
			goto err;
		}
	}

	ret = recv_prep(&ring, &iov, rd->buf_select ? BUF_BGID : 0);
	if (ret) {
		fprintf(stderr, "recv_prep failed: %d\n", ret);
		goto err;
	}

	pthread_mutex_unlock(mutex);
	ret = do_recvmsg(&ring, &iov, rd);

	io_uring_queue_exit(&ring);

err:
	return (void *)(intptr_t)ret;
out:
	pthread_mutex_unlock(mutex);
	io_uring_queue_exit(&ring);
	return NULL;
}

static int do_sendmsg(void)
{
	struct sockaddr_in saddr;
	struct iovec iov = {
		.iov_base = str,
		.iov_len = sizeof(str),
	};
	struct msghdr msg;
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

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &saddr;
	msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_sendmsg(sqe, sockfd, &msg, 0);

	ret = io_uring_submit(&ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(&ring, &cqe);
	if (cqe->res < 0) {
		fprintf(stderr, "%s: failed cqe: %d\n", __FUNCTION__, cqe->res);
		goto err;
	}

	close(sockfd);
	return 0;
err:
	close(sockfd);
	return 1;
}

static int test(int buf_select, int no_buf_add)
{
	struct recv_data rd;
	pthread_mutexattr_t attr;
	pthread_t recv_thread;
	pthread_mutex_t mutex;
	int ret;
	void *retval;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, 1);
	pthread_mutex_init(&mutex, &attr);
	pthread_mutex_lock(&mutex);

	rd.mutex = &mutex;
	rd.buf_select = buf_select;
	rd.no_buf_add = no_buf_add;
	ret = pthread_create(&recv_thread, NULL, recv_fn, &rd);
	if (ret) {
		fprintf(stderr, "Thread create failed\n");
		return 1;
	}

	pthread_mutex_lock(&mutex);
	do_sendmsg();
	pthread_join(recv_thread, &retval);
	ret = (int)(intptr_t)retval;

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	if (argc > 1)
		return 0;

	ret = test(0, 0);
	if (ret) {
		fprintf(stderr, "send_recvmsg 0 failed\n");
		return 1;
	}

	ret = test(1, 0);
	if (ret) {
		fprintf(stderr, "send_recvmsg 1 0 failed\n");
		return 1;
	}

	ret = test(1, 1);
	if (ret) {
		fprintf(stderr, "send_recvmsg 1 1 failed\n");
		return 1;
	}

	return 0;
}
