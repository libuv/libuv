/* Copyright libuv project contributors. All rights reserved.
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

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>

/*
 * On Windows Server 2025 (NT build 26100+), opening stdio file descriptors
 * with uv_pipe_open() leaves the resulting handle implicitly keeping the
 * event loop alive even after all write work has drained — see
 * https://github.com/libuv/libuv/issues/5147. The expected workaround in
 * src/win/pipe.c is to unref the pipe handle when it wraps a stdio fd.
 *
 * This test verifies that contract on the running OS:
 *   - On Win2025+, after uv_pipe_open(fd) where fd in {0,1,2} and the
 *     underlying OS handle is a pipe, the resulting uv_pipe_t MUST
 *     report uv_has_ref() == 0.
 *   - On older Windows (build < 26100), the handle MAY remain refed
 *     (the original behavior); the test only asserts on Win2025+.
 *
 * Because the test runner inherits whatever stdout the harness was launched
 * with — typically a console, a file, or a redirected handle that is not
 * actually a Win32 named pipe — we cannot just call uv_pipe_open() on the
 * inherited fd 1: uv__set_pipe_handle's SetNamedPipeHandleState would
 * reject a non-pipe with UV_ENOTSOCK. So we manufacture a pipe with
 * CreatePipe(), wrap its read end as a CRT fd with _open_osfhandle(),
 * dup it onto fd 1 (saving the original so we can restore for the
 * runner's TAP output), call uv_pipe_open() on fd 1, restore fd 1, and
 * then make our assertions.
 *
 * Skipped on non-Windows.
 */

static int is_win2025_or_newer(void) {
  typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW*);
  HMODULE ntdll;
  RtlGetVersion_t fn;
  OSVERSIONINFOW os_info;

  ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == NULL) return 0;
  fn = (RtlGetVersion_t)(uintptr_t) GetProcAddress(ntdll, "RtlGetVersion");
  if (fn == NULL) return 0;

  os_info.dwOSVersionInfoSize = sizeof(os_info);
  if (fn(&os_info) != 0 /* STATUS_SUCCESS */) return 0;

  return os_info.dwBuildNumber >= 26100;
}

TEST_IMPL(pipe_stdio_no_keep_alive_win) {
  HANDLE pipe_read = INVALID_HANDLE_VALUE;
  HANDLE pipe_write = INVALID_HANDLE_VALUE;
  int crt_fd = -1;
  int saved_stdout_fd = -1;
  uv_pipe_t pipe_stdout;
  int has_ref;
  int run_result;
  int open_result;

  if (!is_win2025_or_newer()) {
    RETURN_SKIP("Test requires Windows Server 2025 (build >= 26100).");
  }

  /* Manufacture an overlapped pipe so uv_pipe_open's IOCP-attach path
   * exercises the same code that the real stdio bug surfaces in. */
  if (!CreatePipe(&pipe_read, &pipe_write, NULL, 0)) {
    fprintf(stderr, "CreatePipe failed: %lu\n", GetLastError());
    return TEST_SKIP;
  }

  crt_fd = _open_osfhandle((intptr_t) pipe_read, _O_RDONLY | _O_BINARY);
  if (crt_fd < 0) {
    CloseHandle(pipe_read);
    CloseHandle(pipe_write);
    return TEST_SKIP;
  }
  /* _open_osfhandle takes ownership of pipe_read; it is closed via _close(crt_fd). */
  pipe_read = INVALID_HANDLE_VALUE;

  /* Save the runner's real stdout so we can restore for TAP output. */
  saved_stdout_fd = _dup(1);
  ASSERT_GE(saved_stdout_fd, 0);

  /* Swap our pipe onto fd 1. uv_pipe_open(pipe, 1) below will see file<=2,
   * take the stdio code path (DuplicateHandle, was_stdio = 1), and exercise
   * the Win2025 unref. */
  if (_dup2(crt_fd, 1) != 0) {
    _close(saved_stdout_fd);
    _close(crt_fd);
    CloseHandle(pipe_write);
    return TEST_SKIP;
  }

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_stdout, 0));
  open_result = uv_pipe_open(&pipe_stdout, 1);

  /* Restore stdout immediately so subsequent printf / ASSERT messages
   * land in the runner's output rather than the manufactured pipe. */
  _dup2(saved_stdout_fd, 1);
  _close(saved_stdout_fd);
  _close(crt_fd);

  ASSERT_OK(open_result);

  /* On Win2025+, the handle must not keep the loop alive. */
  has_ref = uv_has_ref((const uv_handle_t*) &pipe_stdout);
  ASSERT_EQ(has_ref, 0);

  /* The loop should immediately report no remaining work. */
  run_result = uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  ASSERT_EQ(run_result, 0);

  uv_close((uv_handle_t*) &pipe_stdout, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  /* uv_pipe_open duplicated the underlying handle and owns the duplicate;
   * the OS handle we passed in via fd 1 / crt_fd was closed when we did
   * _close(crt_fd) above. The write end of the manufactured pipe still
   * needs to be cleaned up. */
  CloseHandle(pipe_write);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else /* _WIN32 */

TEST_IMPL(pipe_stdio_no_keep_alive_win) {
  RETURN_SKIP("Test is Windows-only.");
}

#endif /* _WIN32 */
