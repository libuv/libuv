/* SPDX-License-Identifier: MIT */
/*
 * Description: test CQ ring sizing
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "liburing.h"

int main(int argc, char *argv[])
{
	struct io_uring_params p;
	struct io_uring ring;
	int ret;

	if (argc > 1)
		return 0;

	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_CQSIZE;
	p.cq_entries = 64;

	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret) {
		if (ret == -EINVAL) {
			printf("Skipped, not supported on this kernel\n");
			goto done;
		}
		printf("ring setup failed\n");
		return 1;
	}

	if (p.cq_entries < 64) {
		printf("cq entries invalid (%d)\n", p.cq_entries);
		goto err;
	}
	io_uring_queue_exit(&ring);

	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_CQSIZE;
	p.cq_entries = 0;

	ret = io_uring_queue_init_params(4, &ring, &p);
	if (ret >= 0 || errno != EINVAL) {
		printf("zero sized cq ring succeeded\n");
		goto err;
	}

done:
	return 0;
err:
	io_uring_queue_exit(&ring);
	return 1;
}
