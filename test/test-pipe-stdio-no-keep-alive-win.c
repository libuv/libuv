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

/*
 * On Windows Server 2025 (NT build 26100+), opening stdio file descriptors
 * with uv_pipe_open() leaves the resulting handle implicitly keeping the
 * event loop alive even after all write work has drained — see
 * https://github.com/libuv/libuv/issues/5147. The expected workaround in src/win/pipe.c is to
 * unref the pipe handle when it wraps a stdio fd.
 *
 * This test verifies that contract on the running OS:
 *   - On Win2025+, after uv_pipe_open(fd) where fd in {0,1,2}, the
 *     resulting handle MUST report uv_has_ref() == 0.
 *   - On older Windows (build < 26100), the handle MAY remain refed
 *     (the original behavior); the test only asserts on Win2025+.
 *
 * Skipped on non-Windows.
 *
 * NB: this test deliberately does not actually drive any I/O through the
 * inherited stdout pipe — that would interleave with the test runner's
 * own output. We only need to observe the post-open ref state.
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
  uv_pipe_t pipe_stdout;
  int r;

  if (!is_win2025_or_newer()) {
    fprintf(stderr, "Skipping: requires Windows Server 2025 (build >= 26100)\n");
    fflush(stderr);
    return 0;
  }

  /* Use fd 1 (stdout) — the duplicate-and-forget logic in uv_pipe_open
   * means we are not at risk of disturbing the test runner's actual
   * stdout because we never read/write through pipe_stdout here. */
  r = uv_pipe_init(uv_default_loop(), &pipe_stdout, 0);
  ASSERT_OK(r);

  r = uv_pipe_open(&pipe_stdout, 1);
  /* uv_pipe_open succeeds for stdio fds on Windows. */
  ASSERT_OK(r);

  /* After uv_pipe_open() on stdio fd, on Win2025+, the handle must NOT
   * keep the loop alive. */
  ASSERT_EQ(uv_has_ref((const uv_handle_t*) &pipe_stdout), 0);

  /* And uv_run with UV_RUN_NOWAIT should not deadlock and should report
   * the loop as having no other refed work. */
  r = uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  /* Returns 0 when the loop has nothing else to do. */
  ASSERT_EQ(r, 0);

  uv_close((uv_handle_t*) &pipe_stdout, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else /* _WIN32 */

TEST_IMPL(pipe_stdio_no_keep_alive_win) {
  RETURN_SKIP("Test is Windows-only.");
}

#endif /* _WIN32 */
