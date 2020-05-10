/* SPDX-License-Identifier: MIT */
#ifndef LIBURING_BARRIER_H
#define LIBURING_BARRIER_H

/*
From the kernel documentation file refcount-vs-atomic.rst:

A RELEASE memory ordering guarantees that all prior loads and
stores (all po-earlier instructions) on the same CPU are completed
before the operation. It also guarantees that all po-earlier
stores on the same CPU and all propagated stores from other CPUs
must propagate to all other CPUs before the release operation
(A-cumulative property). This is implemented using
:c:func:`smp_store_release`.

An ACQUIRE memory ordering guarantees that all post loads and
stores (all po-later instructions) on the same CPU are
completed after the acquire operation. It also guarantees that all
po-later stores on the same CPU must propagate to all other CPUs
after the acquire operation executes. This is implemented using
:c:func:`smp_acquire__after_ctrl_dep`.
*/

/* From tools/include/linux/compiler.h */
/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define io_uring_barrier()	__asm__ __volatile__("": : :"memory")

/* From tools/virtio/linux/compiler.h */
#define IO_URING_WRITE_ONCE(var, val) \
	(*((volatile __typeof(val) *)(&(var))) = (val))
#define IO_URING_READ_ONCE(var) (*((volatile __typeof(var) *)(&(var))))


#if defined(__x86_64__) || defined(__i386__)
/* Adapted from arch/x86/include/asm/barrier.h */
#define io_uring_smp_store_release(p, v)	\
do {						\
	io_uring_barrier();			\
	IO_URING_WRITE_ONCE(*(p), (v));		\
} while (0)

#define io_uring_smp_load_acquire(p)			\
({							\
	__typeof(*p) ___p1 = IO_URING_READ_ONCE(*(p));	\
	io_uring_barrier();				\
	___p1;						\
})

#else /* defined(__x86_64__) || defined(__i386__) */
/*
 * Add arch appropriate definitions. Use built-in atomic operations for
 * archs we don't have support for.
 */
#define io_uring_smp_store_release(p, v) \
	__atomic_store_n(p, v, __ATOMIC_RELEASE)
#define io_uring_smp_load_acquire(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#endif /* defined(__x86_64__) || defined(__i386__) */

#endif /* defined(LIBURING_BARRIER_H) */
