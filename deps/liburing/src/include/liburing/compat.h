#ifndef LIBURING_COMPAT_H
#define LIBURING_COMPAT_H

#include <stdint.h>
#include <inttypes.h>

struct uv__kernel_timespec {
	int64_t		tv_sec;
	long long	tv_nsec;
};

typedef int uv__kernel_rwf_t;

struct uv__open_how {
	uint64_t	flags;
	uint64_t	mode;
	uint64_t	resolve;
};

#endif
