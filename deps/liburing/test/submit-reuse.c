/* SPDX-License-Identifier: MIT */
/*
 * Test reads that will punt to blocking context, with immediate overwrite
 * of iovec->iov_base to NULL. If the kernel doesn't properly handle
 * reuse of the iovec, we should get -EFAULT.
 */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include "liburing.h"

#define STR_SIZE	32768
#define FILE_SIZE	65536

struct thread_data {
	int fd1, fd2;
	volatile int do_exit;
};

static void *flusher(void *__data)
{
	struct thread_data *data = __data;
	int i = 0;

	while (!data->do_exit) {
		posix_fadvise(data->fd1, 0, FILE_SIZE, POSIX_FADV_DONTNEED);
		posix_fadvise(data->fd2, 0, FILE_SIZE, POSIX_FADV_DONTNEED);
		i++;
	}

	return NULL;
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
	fsync(fd);
	close(fd);
	return ret != FILE_SIZE;
}

static char str1[STR_SIZE];
static char str2[STR_SIZE];

static struct io_uring ring;

static int prep(int fd, char *str)
{
	struct io_uring_sqe *sqe;
	struct iovec iov = {
		.iov_base = str,
		.iov_len = STR_SIZE,
	};
	int ret;

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_readv(sqe, fd, &iov, 1, 0);
	sqe->user_data = fd;
	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "submit got %d\n", ret);
		return 1;
	}
	iov.iov_base = NULL;
	return 0;
}

static int wait_nr(int nr)
{
	int i, ret;

	for (i = 0; i < nr; i++) {
		struct io_uring_cqe *cqe;

		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret)
			return ret;
		if (cqe->res < 0) {
			fprintf(stderr, "cqe->res=%d\n", cqe->res);
			return 1;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	return 0;
}

static unsigned long long mtime_since(const struct timeval *s,
				      const struct timeval *e)
{
	long long sec, usec;

	sec = e->tv_sec - s->tv_sec;
	usec = (e->tv_usec - s->tv_usec);
	if (sec > 0 && usec < 0) {
		sec--;
		usec += 1000000;
	}

	sec *= 1000;
	usec /= 1000;
	return sec + usec;
}

static unsigned long long mtime_since_now(struct timeval *tv)
{
	struct timeval end;

	gettimeofday(&end, NULL);
	return mtime_since(tv, &end);
}

int main(int argc, char *argv[])
{
	struct thread_data data;
	int fd1, fd2, ret, i;
	struct timeval tv;
	pthread_t thread;
	void *tret;

	if (argc > 1)
		return 0;

	ret = io_uring_queue_init(32, &ring, 0);
	if (ret) {
		fprintf(stderr, "io_uring_queue_init: %d\n", ret);
		return 1;
	}

	if (create_file(".reuse.1")) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}
	if (create_file(".reuse.2")) {
		fprintf(stderr, "file creation failed\n");
		goto err;
	}

	fd1 = open(".reuse.1", O_RDONLY);
	fd2 = open(".reuse.2", O_RDONLY);

	data.fd1 = fd1;
	data.fd2 = fd2;
	data.do_exit = 0;
	pthread_create(&thread, NULL, flusher, &data);
	usleep(10000);

	gettimeofday(&tv, NULL);
	for (i = 0; i < 1000; i++) {
		ret = prep(fd1, str1);
		if (ret) {
			fprintf(stderr, "prep1 failed: %d\n", ret);
			goto err;
		}
		ret = prep(fd2, str2);
		if (ret) {
			fprintf(stderr, "prep1 failed: %d\n", ret);
			goto err;
		}
		ret = wait_nr(2);
		if (ret) {
			fprintf(stderr, "wait_nr: %d\n", ret);
			goto err;
		}
		if (mtime_since_now(&tv) > 5000)
			break;
	}

	data.do_exit = 1;
	pthread_join(thread, &tret);

	close(fd2);
	close(fd1);
	io_uring_queue_exit(&ring);
	unlink(".reuse.1");
	unlink(".reuse.2");
	return 0;
err:
	io_uring_queue_exit(&ring);
	unlink(".reuse.1");
	unlink(".reuse.2");
	return 1;
}
