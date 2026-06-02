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

#ifdef _WIN32

#include "uv.h"
#include "task.h"

#include <windows.h>
#include <io.h>

/*
 * Reproducer A (in-process) for https://github.com/libuv/libuv/issues/5147.
 *
 * On Windows Server 2025 (NT build 26100+), associating a stdio pipe handle
 * with the loop's IOCP via uv_pipe_open(fd in {0,1,2}) leaves the loop alive
 * even though the handle is inactive. This test manufactures a pipe, swaps it
 * onto fd 1, opens it with uv_pipe_open(), then drives uv_run(UV_RUN_DEFAULT)
 * under an UNREF'd watchdog timer:
 *
 *   - Healthy OS: the inactive pipe does not keep the loop alive, so uv_run
 *     returns before the watchdog fires -> PASS.
 *   - Win2025 with the workaround disabled (#5147): the loop stays alive, the
 *     watchdog fires, dumps loop diagnostics, and stops the loop -> the
 *     watchdog-fired assertion FAILS (intended canary; revert with the fix).
 *
 * Skipped on Windows older than build 26100 (bug not present).
 */

/* TEMPORARY: revert before merge — #5147. Defined in src/win/core.c and
 * UV_EXTERN-exported there so this shared-lib-linked test can resolve it. */
UV_EXTERN void uv__loop_debug_dump(uv_loop_t* loop, const char* tag);

static int watchdog_fired;

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

static void watchdog_cb(uv_timer_t* handle) {
  watchdog_fired = 1;
  uv__loop_debug_dump(handle->loop, "reproducer A: loop still alive at watchdog");
  uv_stop(handle->loop);
}

TEST_IMPL(pipe_stdio_no_keep_alive_win) {
  uv_file fds[2];
  int saved_stdout_fd = -1;
  uv_pipe_t pipe_stdout;
  uv_timer_t watchdog;
  int open_result;

  if (!is_win2025_or_newer()) {
    RETURN_SKIP("Test requires Windows Server 2025 (build >= 26100).");
  }

  /* Manufacture an OVERLAPPED pipe (UV_NONBLOCK_PIPE => FILE_FLAG_OVERLAPPED)
   * so uv_pipe_open's IOCP-attach path -- the path we suspect is involved
   * in #5147 (root cause unconfirmed) -- is actually exercised. A plain
   * CreatePipe() produces a synchronous pipe, which uv__set_pipe_handle
   * routes through the non-overlapped branch that never associates with the
   * loop's IOCP. */
  ASSERT_OK(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE));
  /* fds[0] = read end, fds[1] = write end (like stdout). Keep the read end
   * open as the peer holding the pipe; place the write end on fd 1 so
   * uv_pipe_open(pipe, 1) takes the stdio code path (0 <= file <= 2). */

  saved_stdout_fd = _dup(1);
  ASSERT_GE(saved_stdout_fd, 0);
  ASSERT_OK(_dup2(fds[1], 1));

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_stdout, 0));
  open_result = uv_pipe_open(&pipe_stdout, 1);

  /* Restore stdout immediately so subsequent printf / ASSERT messages land
   * in the runner's output. uv_pipe_open duplicated the underlying handle, so
   * we can drop our own write-end fd now. */
  _dup2(saved_stdout_fd, 1);
  _close(saved_stdout_fd);
  _close(fds[1]);

  ASSERT_OK(open_result);

  uv__loop_debug_dump(uv_default_loop(), "reproducer A: after uv_pipe_open(fd=1)");

  /* Unref'd watchdog: fires only if something else keeps the loop alive. */
  watchdog_fired = 0;
  ASSERT_OK(uv_timer_init(uv_default_loop(), &watchdog));
  ASSERT_OK(uv_timer_start(&watchdog, watchdog_cb, 5000, 0));
  uv_unref((uv_handle_t*) &watchdog);

  /* Healthy: returns immediately (idle pipe does not keep loop alive).
   * Win2025 (workaround off): stays alive until the watchdog fires. */
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  /* Canary: on Win2025 with the workaround disabled this fails because the
   * loop did not drain. This assertion is the permanent contract; when the
   * fix lands, what changes is the #if 0 workaround in src/win/pipe.c and the
   * Win2025-only skip guard, not this assert. */
  ASSERT_OK(watchdog_fired);

  uv_close((uv_handle_t*) &watchdog, NULL);
  uv_close((uv_handle_t*) &pipe_stdout, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  _close(fds[0]);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else /* _WIN32 */

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif /* _WIN32 */
