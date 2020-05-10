/* SPDX-License-Identifier: MIT */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "liburing/compat.h"
#include "liburing/io_uring.h"
#include "liburing.h"

#include "syscall.h"

int io_uring_register_buffers(struct io_uring *ring, const struct iovec *iovecs,
			      unsigned nr_iovecs)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_BUFFERS,
					iovecs, nr_iovecs);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_unregister_buffers(struct io_uring *ring)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_UNREGISTER_BUFFERS,
					NULL, 0);
	if (ret < 0)
		return -errno;

	return 0;
}

/*
 * Register an update for an existing file set. The updates will start at
 * 'off' in the original array, and 'nr_files' is the number of files we'll
 * update.
 *
 * Returns number of files updated on success, -ERROR on failure.
 */
int io_uring_register_files_update(struct io_uring *ring, unsigned off,
				   int *files, unsigned nr_files)
{
	struct io_uring_files_update up = {
		.offset	= off,
		.fds	= (unsigned long) files,
	};
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd,
					IORING_REGISTER_FILES_UPDATE, &up,
					nr_files);
	if (ret < 0)
		return -errno;

	return ret;
}

int io_uring_register_files(struct io_uring *ring, const int *files,
			      unsigned nr_files)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_FILES,
					files, nr_files);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_unregister_files(struct io_uring *ring)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_UNREGISTER_FILES,
					NULL, 0);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_register_eventfd(struct io_uring *ring, int event_fd)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_EVENTFD,
					&event_fd, 1);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_unregister_eventfd(struct io_uring *ring)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_UNREGISTER_EVENTFD,
					NULL, 0);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_register_eventfd_async(struct io_uring *ring, int event_fd)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_EVENTFD_ASYNC,
			&event_fd, 1);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_register_probe(struct io_uring *ring, struct io_uring_probe *p,
			    unsigned int nr_ops)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_PROBE,
					p, nr_ops);
	if (ret < 0)
		return -errno;

	return 0;
}

int io_uring_register_personality(struct io_uring *ring)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_REGISTER_PERSONALITY,
					NULL, 0);
	if (ret < 0)
		return -errno;

	return ret;
}

int io_uring_unregister_personality(struct io_uring *ring, int id)
{
	int ret;

	ret = __sys_io_uring_register(ring->ring_fd, IORING_UNREGISTER_PERSONALITY,
					NULL, id);
	if (ret < 0)
		return -errno;

	return ret;
}
