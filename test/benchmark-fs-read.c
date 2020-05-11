/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "task.h"
#include "uv.h"

#include <stdio.h>
#include <stdlib.h>

#define NUM_SYNC_REQS         (10 * 1e5)
#define NUM_ASYNC_REQS        (1 * (int) 1e5)
#define MAX_CONCURRENT_REQS   32

struct async_req {
  uv_fs_t fs_req;
  int* count;
  uv_buf_t iov;
  char scratch[256];
};

static uv_fs_t open_req;


static void warmup(const char* path) {
  uv_buf_t iov;
  uv_fs_t reqs[MAX_CONCURRENT_REQS];
  unsigned int i;
  int r;
  char scratch[256];

  r = uv_fs_open(NULL, &open_req, path, O_RDONLY, 0, NULL);
  ASSERT(r == 0);
  ASSERT(open_req.result >= 0);
  open_req.file = open_req.result;

  /* warm up the thread pool and fs cache */
  iov = uv_buf_init(scratch, 256);
  for (i = 0; i < ARRAY_SIZE(reqs); i++) {
    r = uv_fs_read(uv_default_loop(), reqs + i, open_req.file, &iov, 1, 0, uv_fs_req_cleanup);
    ASSERT(r == 0);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}


static void sync_bench(const char* path) {
  uint64_t before;
  uint64_t after;
  uv_fs_t req;
  int i;
  char scratch[256];
  uv_buf_t iov;

  /* do the sync benchmark */
  before = uv_hrtime();

  iov = uv_buf_init(scratch, 256);

  for (i = 0; i < NUM_SYNC_REQS; i++) {
    uv_fs_read(NULL, &req, open_req.file, &iov, 1, 0, NULL);
  }
  ASSERT(req.result == 256);

  after = uv_hrtime();

  printf("%s reads (sync): %.2fs (%s/s)\n",
         fmt(1.0 * NUM_SYNC_REQS),
         (after - before) / 1e9,
         fmt((1.0 * NUM_SYNC_REQS) / ((after - before) / 1e9)));
  fflush(stdout);
}


static void read_cb(uv_fs_t* fs_req) {
  struct async_req* req = container_of(fs_req, struct async_req, fs_req);
  uv_fs_req_cleanup(&req->fs_req);
  if (*req->count == 0) {
    ASSERT(fs_req->result == 256);
    return;
  }
  uv_fs_read(uv_default_loop(), &req->fs_req, open_req.file, &req->iov, 1, 0, read_cb);
  (*req->count)--;
}


static void async_bench(const char* path) {
  struct async_req reqs[MAX_CONCURRENT_REQS];
  struct async_req* req;
  uint64_t before;
  uint64_t after;
  int count;
  int i;

  for (i = 0; i < MAX_CONCURRENT_REQS; i++)
    reqs[i].iov = uv_buf_init(reqs[i].scratch, 256);

  for (i = 1; i <= MAX_CONCURRENT_REQS; i++) {
    count = NUM_ASYNC_REQS;

    for (req = reqs; req < reqs + i; req++) {
      req->count = &count;
      uv_fs_read(uv_default_loop(), &req->fs_req, open_req.file, &req->iov, 1, 0, read_cb);
    }

    before = uv_hrtime();
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    after = uv_hrtime();

    printf("%s reads (%d concurrent): %.2fs (%s/s)\n",
           fmt(1.0 * NUM_ASYNC_REQS),
           i,
           (after - before) / 1e9,
           fmt((1.0 * NUM_ASYNC_REQS) / ((after - before) / 1e9)));
    fflush(stdout);
  }
}


BENCHMARK_IMPL(fs_read) {
  const char path[] = "test/fixtures/lorem_ipsum.txt";
  warmup(path);
  sync_bench(path);
  async_bench(path);
  MAKE_VALGRIND_HAPPY();
  return 0;
}
