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

#include "uv.h"
#include "task.h"

#ifdef _WIN32
# include <io.h>
# include <windows.h>
#else /*  Unix */
# include <fcntl.h>
# include <unistd.h>
#endif

/*
 * These tests currently directly use the underlying OS primitives to
 * create handles (file descriptors) under test. This approach makes
 * the most sense as guess_handle is primarily used to determine the
 * type of handles passed to an application using libuv (rather than
 * being used on handles created by libuv itself).
 *
 * TODO: More comprehensive _WIN32 tests are required. Existing WIN32
 * tests are copied from appropriate sections of the TTY test.
 */
TEST_IMPL(guess_handle) {
  int ttyin_fd, ttyout_fd;
#ifdef _WIN32
  HANDLE handle;
#else
  int pipe_fds[2];
  int r;
  int afunix_stream;
  int afinet_stream;
  int afinet6_stream;
  int afinet_dgram;
  int afinet6_dgram;
  int regfile_fd;
  int dir_fd;
  int devnull_fd;
#endif

#ifdef _WIN32
  handle = CreateFileA("conin$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT(handle != INVALID_HANDLE_VALUE);
  ttyin_fd = _open_osfhandle((intptr_t) handle, 0);

  handle = CreateFileA("conout$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT(handle != INVALID_HANDLE_VALUE);
  ttyout_fd = _open_osfhandle((intptr_t) handle, 0);

#else /* unix */
  ttyin_fd = open("/dev/tty", O_RDONLY, 0);
  ttyout_fd = open("/dev/tty", O_WRONLY, 0);
  r = pipe(pipe_fds);
  afunix_stream = socket(AF_UNIX, SOCK_STREAM, 0);
  afinet_stream = socket(AF_INET, SOCK_STREAM, 0);
  afinet6_stream = socket(AF_INET6, SOCK_STREAM, 0);
  afinet_dgram = socket(AF_INET, SOCK_DGRAM, 0);
  afinet6_dgram = socket(AF_INET6, SOCK_DGRAM, 0);
  regfile_fd = open("test_file", O_RDONLY | O_CREAT, S_IWRITE | S_IREAD);
  dir_fd = open(".", O_RDONLY, 0);
  devnull_fd = open("/dev/null", O_RDONLY, 0);
#endif

  ASSERT(ttyin_fd >= 0);
  ASSERT(ttyout_fd >= 0);
#ifndef _WIN32
  ASSERT(r == 0);
  ASSERT(afunix_stream >= 0);
  ASSERT(afinet_stream >= 0);
  ASSERT(afinet6_stream >= 0);
  ASSERT(afinet_dgram >= 0);
  ASSERT(afinet6_dgram >= 0);
  ASSERT(regfile_fd >= 0);
  ASSERT(dir_fd >= 0);
#endif

  ASSERT(UV_UNKNOWN_HANDLE == uv_guess_handle(-1));
  ASSERT(UV_TTY == uv_guess_handle(ttyin_fd));
  ASSERT(UV_TTY == uv_guess_handle(ttyout_fd));

#ifndef _WIN32
  ASSERT(UV_NAMED_PIPE == uv_guess_handle(pipe_fds[0]));
  ASSERT(UV_NAMED_PIPE == uv_guess_handle(pipe_fds[1]));
  ASSERT(UV_NAMED_PIPE == uv_guess_handle(afunix_stream));
  ASSERT(UV_TCP == uv_guess_handle(afinet_stream));
  ASSERT(UV_TCP == uv_guess_handle(afinet6_stream));
  ASSERT(UV_UDP == uv_guess_handle(afinet_dgram));
  ASSERT(UV_UDP == uv_guess_handle(afinet6_dgram));
  ASSERT(UV_FILE == uv_guess_handle(regfile_fd));
  ASSERT(UV_UNKNOWN_HANDLE == uv_guess_handle(dir_fd));
  ASSERT(UV_UNKNOWN_HANDLE == uv_guess_handle(devnull_fd));
#endif

  close(ttyin_fd);
  close(ttyout_fd);

#ifndef _WIN32
  close(pipe_fds[0]);
  close(pipe_fds[1]);
  close(afunix_stream);
  close(afinet_stream);
  close(afinet6_stream);
  close(afinet_dgram);
  close(afinet6_dgram);
  close(regfile_fd);
  unlink("test_file");
  close(dir_fd);
  close(devnull_fd);
#endif
  /* FIXME: Is anything else required to close the windows
     handles? */

  MAKE_VALGRIND_HAPPY();
  return 0;
}
