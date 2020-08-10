/* SPDX-License-Identifier: MIT */
/*
 * io_uring_setup.c
 *
 * Description: Unit tests for the io_uring_setup system call.
 *
 * Copyright 2019, Red Hat, Inc.
 * Author: Jeff Moyer <jmoyer@redhat.com>
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include "liburing.h"

#include "../syscall.h"

char *features_string(struct io_uring_params *p)
{
	static char flagstr[64];

	if (!p || !p->features)
		return "none";

	if (p->features & ~IORING_FEAT_SINGLE_MMAP) {
		snprintf(flagstr, 64, "0x%.8x", p->features);
		return flagstr;
	}

	if (p->features & IORING_FEAT_SINGLE_MMAP)
		strncat(flagstr, "IORING_FEAT_SINGLE_MMAP", 64 - strlen(flagstr));

	return flagstr;
}

/*
 * Attempt the call with the given args.  Return 0 when expect matches
 * the return value of the system call, 1 otherwise.
 */
char *
flags_string(struct io_uring_params *p)
{
	static char flagstr[64];
	int add_pipe = 0;

	memset(flagstr, 0, sizeof(flagstr));

	if (!p || p->flags == 0)
		return "none";

	/*
	 * If unsupported flags are present, just print the bitmask.
	 */
	if (p->flags & ~(IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL |
			 IORING_SETUP_SQ_AFF)) {
		snprintf(flagstr, 64, "0x%.8x", p->flags);
		return flagstr;
	}

	if (p->flags & IORING_SETUP_IOPOLL) {
		strncat(flagstr, "IORING_SETUP_IOPOLL", 64 - strlen(flagstr));
		add_pipe = 1;
	}
	if (p->flags & IORING_SETUP_SQPOLL) {
		if (add_pipe)
			strncat(flagstr, "|", 64 - strlen(flagstr));
		else
			add_pipe = 1;
		strncat(flagstr, "IORING_SETUP_SQPOLL", 64 - strlen(flagstr));
	}
	if (p->flags & IORING_SETUP_SQ_AFF) {
		if (add_pipe)
			strncat(flagstr, "|", 64 - strlen(flagstr));
		strncat(flagstr, "IORING_SETUP_SQ_AFF", 64 - strlen(flagstr));
	}

	return flagstr;
}

char *
dump_resv(struct io_uring_params *p)
{
	static char resvstr[4096];

	if (!p)
		return "";

	sprintf(resvstr, "0x%.8x 0x%.8x 0x%.8x", p->resv[0],
		p->resv[1], p->resv[2]);

	return resvstr;
}

/* bogus: setup returns a valid fd on success... expect can't predict the
   fd we'll get, so this really only takes 1 parameter: error */
int
try_io_uring_setup(unsigned entries, struct io_uring_params *p, int expect, int error)
{
	int ret, __errno;

	printf("io_uring_setup(%u, %p), flags: %s, feat: %s, resv: %s, sq_thread_cpu: %u\n",
	       entries, p, flags_string(p), features_string(p), dump_resv(p),
	       p ? p->sq_thread_cpu : 0);

	ret = __sys_io_uring_setup(entries, p);
	if (ret != expect) {
		printf("expected %d, got %d\n", expect, ret);
		/* if we got a valid uring, close it */
		if (ret > 0)
			close(ret);
		return 1;
	}
	__errno = errno;
	if (expect == -1 && error != __errno) {
		if (__errno == EPERM && geteuid() != 0) {
			printf("Needs root, not flagging as an error\n");
			return 0;
		}
		printf("expected errno %d, got %d\n", error, __errno);
		return 1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int fd;
	unsigned int status = 0;
	struct io_uring_params p;

	if (argc > 1)
		return 0;

	memset(&p, 0, sizeof(p));
	status |= try_io_uring_setup(0, &p, -1, EINVAL);
	status |= try_io_uring_setup(1, NULL, -1, EFAULT);

	/* resv array is non-zero */
	memset(&p, 0, sizeof(p));
	p.resv[0] = p.resv[1] = p.resv[2] = 1;
	status |= try_io_uring_setup(1, &p, -1, EINVAL);

	/* invalid flags */
	memset(&p, 0, sizeof(p));
	p.flags = ~0U;
	status |= try_io_uring_setup(1, &p, -1, EINVAL);

	/* IORING_SETUP_SQ_AFF set but not IORING_SETUP_SQPOLL */
	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_SQ_AFF;
	status |= try_io_uring_setup(1, &p, -1, EINVAL);

	/* attempt to bind to invalid cpu */
	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
	p.sq_thread_cpu = get_nprocs_conf();
	status |= try_io_uring_setup(1, &p, -1, EINVAL);

	/* I think we can limit a process to a set of cpus.  I assume
	 * we shouldn't be able to setup a kernel thread outside of that.
	 * try to do that. (task->cpus_allowed) */

	/* read/write on io_uring_fd */
	memset(&p, 0, sizeof(p));
	fd = __sys_io_uring_setup(1, &p);
	if (fd < 0) {
		printf("io_uring_setup failed with %d, expected success\n",
		       errno);
		status = 1;
	} else {
		char buf[4096];
		int ret;
		ret = read(fd, buf, 4096);
		if (ret >= 0) {
			printf("read from io_uring fd succeeded.  expected fail\n");
			status = 1;
		}
	}

	if (!status) {
		printf("PASS\n");
		return 0;
	}

	printf("FAIL\n");
	return -1;
}
