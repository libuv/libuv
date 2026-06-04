/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#include <string.h> /* memcmp, memset */
#ifndef _WIN32
#include <unistd.h> /* close */
#endif

/* Verify that uv_pipe_open_ex() accepts a native OS handle directly.
 *
 * On Windows uv_pipe_open() treats its argument as a CRT file descriptor and
 * converts it to a HANDLE via _get_osfhandle(). A raw HANDLE (e.g. an
 * inherited anonymous pipe) is in a different namespace and fails with EBADF.
 * uv_pipe_open_ex() takes the HANDLE as-is. On Unix uv_os_fd_t is the file
 * descriptor itself, so the call is equivalent to uv_pipe_open().
 */

#ifdef _WIN32
static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  req->handle = NULL; /* signal completion of write_cb */
}
#endif


TEST_IMPL(pipe_open_ex) {
  uv_pipe_t pipe_handle;
  uv_os_fd_t os_handle;
  uv_buf_t buf;
  char data[] = "test data";
  char readbuf[64];
#ifdef _WIN32
  HANDLE read_handle;
  DWORD bytes_read;
  uv_write_t write_req;
#else
  uv_fs_t req;
  uv_file fd[2];
  int n;
#endif

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_handle, 0));

#ifdef _WIN32
  /* CreatePipe() returns raw anonymous pipe HANDLEs that have no associated
   * CRT file descriptor - the exact scenario uv_pipe_open_ex() exists for.
   * The handles are synchronous, which also exercises the non-overlapped
   * pipe code path. */
  ASSERT(CreatePipe(&read_handle, &os_handle, NULL, 0));
#else
  ASSERT_OK(uv_pipe(fd, 0, 0));
  os_handle = fd[1];
#endif

  /* Open the write end directly from its native OS handle. The handle is
   * owned by pipe_handle after this call. */
  ASSERT_OK(uv_pipe_open_ex(&pipe_handle, os_handle));
  ASSERT_OK(uv_stream_set_blocking((uv_stream_t*) &pipe_handle, 1));

  buf = uv_buf_init(data, sizeof(data));
#ifdef _WIN32
  /* uv_try_write() is not implemented for pipes on Windows. */
  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &pipe_handle,
                     &buf,
                     1,
                     write_cb));
  ASSERT_NOT_NULL(write_req.handle);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));
  ASSERT_NULL(write_req.handle); /* signaled completion of write_cb */
#else
  n = uv_try_write((uv_stream_t*) &pipe_handle, &buf, 1);
  ASSERT_EQ(n, (int) sizeof(data));
#endif

  /* Read the data back from the other end and verify it round-trips. */
  memset(readbuf, 0, sizeof(readbuf));
#ifdef _WIN32
  ASSERT(ReadFile(read_handle, readbuf, sizeof(readbuf), &bytes_read, NULL));
  ASSERT_EQ(bytes_read, (DWORD) sizeof(data));
#else
  buf = uv_buf_init(readbuf, sizeof(readbuf));
  n = uv_fs_read(NULL, &req, fd[0], &buf, 1, -1, NULL);
  ASSERT_EQ(n, (int) sizeof(data));
  uv_fs_req_cleanup(&req);
#endif
  ASSERT_OK(memcmp(readbuf, data, sizeof(data)));

  uv_close((uv_handle_t*) &pipe_handle, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

#ifdef _WIN32
  ASSERT(CloseHandle(read_handle));
#else
  ASSERT_OK(close(fd[0]));
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
