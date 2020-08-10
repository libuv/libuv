/* SPDX-License-Identifier: MIT */
/*
 * Description: run timeout overflow test
 *
 */
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <sys/time.h>

#include "liburing.h"

#define TIMEOUT_MSEC	200
static int not_supported;

static void msec_to_ts(struct __kernel_timespec *ts, unsigned int msec)
{
	ts->tv_sec = msec / 1000;
	ts->tv_nsec = (msec % 1000) * 1000000;
}

static int check_timeout_support()
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct __kernel_timespec ts;
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed: %d\n", ret);
		return 1;
	}
	sqe = io_uring_get_sqe(&ring);
	msec_to_ts(&ts, TIMEOUT_MSEC);
	io_uring_prep_timeout(sqe, &ts, 1, 0);

	ret = io_uring_submit(&ring);
	if (ret < 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	ret = io_uring_wait_cqe(&ring, &cqe);
	if (ret < 0) {
		fprintf(stderr, "wait completion %d\n", ret);
		goto err;
	}

	if (cqe->res == -EINVAL) {
		not_supported = 1;
		fprintf(stdout, "Timeout not supported, ignored\n");
		return 0;
	}

	io_uring_cqe_seen(&ring, cqe);
	io_uring_queue_exit(&ring);
	return 0;
err:
	io_uring_queue_exit(&ring);
	return 1;
}

/*
 * We first setup 4 timeout requests, which require a count value of 1, 1, 2,
 * UINT_MAX, so the sequence is 1, 2, 4, 2. Before really timeout, this 4
 * requests will not lead the change of cq_cached_tail, so as sq_dropped.
 *
 * And before this patch. The order of this four requests will be req1->req2->
 * req4->req3. Actually, it should be req1->req2->req3->req4.
 *
 * Then, if there is 2 nop req. All timeout requests expect req4 will completed
 * successful after the patch. And req1/req2 will completed successful with
 * req3/req4 return -ETIME without this patch!
 */
static int test_timeout_overflow()
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct __kernel_timespec ts;
	struct io_uring ring;
	int i, ret;

	ret = io_uring_queue_init(16, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed: %d\n", ret);
		return 1;
	}

	msec_to_ts(&ts, TIMEOUT_MSEC);
	for (i = 0; i < 4; i++) {
		unsigned num;
		sqe = io_uring_get_sqe(&ring);
		switch (i) {
		case 0:
		case 1:
			num = 1;
			break;
		case 2:
			num = 2;
			break;
		case 3:
			num = UINT_MAX;
			break;
		}
		io_uring_prep_timeout(sqe, &ts, num, 0);
	}

	for (i = 0; i < 2; i++) {
		sqe = io_uring_get_sqe(&ring);
		io_uring_prep_nop(sqe);
		io_uring_sqe_set_data(sqe, (void *) 1);
	}
	ret = io_uring_submit(&ring);
	if (ret < 0) {
		fprintf(stderr, "sqe submit failed: %d\n", ret);
		goto err;
	}

	i = 0;
	while (i < 6) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "wait completion %d\n", ret);
			goto err;
		}

		/*
		 * cqe1: first nop req
		 * cqe2: first timeout req, because of cqe1
		 * cqe3: second timeout req because of cqe1 + cqe2
		 * cqe4: second nop req
		 * cqe5~cqe6: the left three timeout req
		 */
		switch (i) {
		case 0:
		case 3:
			if (io_uring_cqe_get_data(cqe) != (void *) 1) {
				fprintf(stderr, "nop not seen as 1 or 2\n");
				goto err;
			}
			break;
		case 1:
		case 2:
		case 4:
			if (cqe->res == -ETIME) {
				fprintf(stderr, "expected not return -ETIME "
					"for the #%d timeout req\n", i - 1);
				goto err;
			}
			break;
		case 5:
			if (cqe->res != -ETIME) {
				fprintf(stderr, "expected return -ETIME for "
					"the #%d timeout req\n", i - 1);
				goto err;
			}
			break;
		}
		io_uring_cqe_seen(&ring, cqe);
		i++;
	}

	return 0;
err:
	return 1;
}

int main(int argc, char *argv[])
{
	int ret;

	if (argc > 1)
		return 0;

	ret = check_timeout_support();
	if (ret) {
		fprintf(stderr, "check_timeout_support failed: %d\n", ret);
		return 1;
	}

	if (not_supported)
		return 0;

	ret = test_timeout_overflow();
	if (ret) {
		fprintf(stderr, "test_timeout_overflow failed\n");
		return 1;
	}

	return 0;
}
