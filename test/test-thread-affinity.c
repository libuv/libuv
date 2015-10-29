/* Copyright libuv project contributors. All rights reserved.
 */

#include "uv.h"
#include "task.h"

#include <string.h>

static void check_affinity(void* arg) {
  int r;
  char* cpumask = arg;
  int cpumasksize = uv_cpumask_size();

  uv_thread_t tid = uv_thread_self();
  uv_thread_setaffinity(&tid, cpumask, NULL, cpumasksize);
  r = uv_thread_setaffinity(&tid, cpumask + cpumasksize, cpumask, cpumasksize);
  if (r != 0)
    cpumask[0] = cpumask[1] = -1;
}

TEST_IMPL(thread_affinity) {
  int t1first, t1second, t2first, t2second;
  int cpumasksize;
  char* cpumask;
  uv_thread_t threads[2];

  cpumasksize = uv_cpumask_size();
  t1first = cpumasksize * 0;
  t1second = cpumasksize * 1;
  t2first = cpumasksize * 2;
  t2second = cpumasksize * 3;

  cpumask = (char*)calloc(4 * cpumasksize, 1);

  cpumask[t1first  + 1] = cpumask[t1first  + 3] = 1;
  cpumask[t1second + 0] = cpumask[t1second + 2] = 1;

  cpumask[t2first  + 0] = cpumask[t2first  + 2] = 1;
  cpumask[t2second + 1] = cpumask[t2second + 3] = 1;

  ASSERT(0 == uv_thread_create(threads + 0,
                               check_affinity,
                               &cpumask[t1first]));
  ASSERT(0 == uv_thread_create(threads + 1,
                               check_affinity,
                               &cpumask[t2first]));
  ASSERT(0 == uv_thread_join(threads + 0));
  ASSERT(0 == uv_thread_join(threads + 1));

  ASSERT(0 == cpumask[t1first + 0]);
  ASSERT(1 == cpumask[t1first + 1]);
  ASSERT(0 == cpumask[t1first + 2]);
  ASSERT(1 == cpumask[t1first + 3]);

  ASSERT(1 == cpumask[t2first + 0]);
  ASSERT(0 == cpumask[t2first + 1]);
  ASSERT(1 == cpumask[t2first + 2]);
  ASSERT(0 == cpumask[t2first + 3]);

  free(cpumask);

  return 0;
}
