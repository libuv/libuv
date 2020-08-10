/* SPDX-License-Identifier: MIT */
/*
 * Based on description from Al Viro - this demonstrates a leak of the
 * io_uring instance, by sending the io_uring fd over a UNIX socket.
 *
 * See:
 *
 * https://lore.kernel.org/linux-block/20190129192702.3605-1-axboe@kernel.dk/T/#m6c87fc64e4d063786af6ec6fadce3ac1e95d3184
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/fs.h>

#include "liburing.h"
#include "../src/syscall.h"

static int __io_uring_register_files(int ring_fd, int fd1, int fd2)
{
	__s32 fds[2] = { fd1, fd2 };

	return __sys_io_uring_register(ring_fd, IORING_REGISTER_FILES, fds, 2);
}

static int get_ring_fd(void)
{
	struct io_uring_params p;
	int fd;

	memset(&p, 0, sizeof(p));

	fd = __sys_io_uring_setup(2, &p);
	if (fd < 0) {
		perror("io_uring_setup");
		return -1;
	}

	return fd;
}

static void send_fd(int socket, int fd)
{
	char buf[CMSG_SPACE(sizeof(fd))];
	struct cmsghdr *cmsg;
	struct msghdr msg;

	memset(buf, 0, sizeof(buf));
	memset(&msg, 0, sizeof(msg));

	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

	memmove(CMSG_DATA(cmsg), &fd, sizeof(fd));

	msg.msg_controllen = CMSG_SPACE(sizeof(fd));

	if (sendmsg(socket, &msg, 0) < 0)
		perror("sendmsg");
}

int main(int argc, char *argv[])
{
	int sp[2], pid, ring_fd, ret;

	if (argc > 1)
		return 0;

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sp) != 0) {
		perror("Failed to create Unix-domain socket pair\n");
		return 1;
	}

	ring_fd = get_ring_fd();
	if (ring_fd < 0)
		return 1;

	ret = __io_uring_register_files(ring_fd, sp[0], sp[1]);
	if (ret < 0) {
		perror("register files");
		return 1;
	}

	pid = fork();
	if (pid)
		send_fd(sp[0], ring_fd);

	close(ring_fd);
	close(sp[0]);
	close(sp[1]);
	return 0;
}
