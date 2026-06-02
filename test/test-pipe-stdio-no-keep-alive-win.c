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
#include <stdio.h>

/*
 * Reproducer A (in-process) for https://github.com/libuv/libuv/issues/5147.
 *
 * A minimal "open an overlapped stdio pipe and run the loop" test was found to
 * drain cleanly on the Win2025 CI runner, so this version moves closer to the
 * Node.js reproducer that actually hangs: it pipes BOTH stdout (fd 1) and
 * stderr (fd 2) as overlapped pipes, writes to each (like console.log /
 * console.error) and leaves them open, and runs an async filesystem request to
 * completion (mirroring the in-flight I/O — https requests — in the Node
 * repro, whose threadpool completions post to the same loop IOCP the stdio
 * pipes are attached to).
 *
 * The loop is then driven with uv_run(UV_RUN_DEFAULT) under an uv_unref'd
 * watchdog timer:
 *   - Healthy OS: everything completes, the idle pipes do not keep the loop
 *     alive, uv_run returns before the watchdog fires -> PASS.
 *   - Win2025 with the workaround disabled (#5147): if the residual ref the
 *     issue describes is present, the loop stays alive, the watchdog fires,
 *     dumps loop state to stdout (captured by the TAP runner) and stops the
 *     loop -> the watchdog-fired assertion FAILS (intended canary).
 *
 * uv__loop_debug_dump is routed to stdout here (not stderr) because the libuv
 * test runner only surfaces a test's stdout; fd 1/2 are restored to the real
 * console before any dump so the output is not swallowed by the pipes.
 *
 * Skipped on Windows older than build 26100 (bug not present).
 */

/* TEMPORARY: revert before merge — #5147. Defined in src/win/core.c and
 * UV_EXTERN-exported there so this shared-lib-linked test can resolve it. */
UV_EXTERN void uv__loop_debug_dump(uv_loop_t* loop,
                                   const char* tag,
                                   FILE* stream);

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

static void noop_write_cb(uv_write_t* req, int status) {
  (void) req;
  (void) status;
  /* Intentionally leave the pipe open, like Node's process.stdout/stderr. */
}

static void stat_cb(uv_fs_t* req) {
  /* Just let the async (threadpool -> IOCP) request complete. */
  uv_fs_req_cleanup(req);
}

static void watchdog_cb(uv_timer_t* handle) {
  watchdog_fired = 1;
  uv__loop_debug_dump(handle->loop,
                      "reproducer A: loop still alive at watchdog",
                      stdout);
  uv_stop(handle->loop);
}

/* Open the write end of an overlapped pipe onto stdio fd `target` (1 or 2) and
 * wrap it in `pipe`. `fds` receives the pipe pair; the read end (fds[0]) is the
 * peer and must stay open until after the loop run. `saved_fd` receives a dup
 * of the original stdio fd so the caller can restore it. */
static void open_stdio_pipe(uv_pipe_t* pipe,
                            uv_file fds[2],
                            int target,
                            int* saved_fd) {
  ASSERT_OK(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE));
  *saved_fd = _dup(target);
  ASSERT_GE(*saved_fd, 0);
  ASSERT_OK(_dup2(fds[1], target));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), pipe, 0));
  ASSERT_OK(uv_pipe_open(pipe, target));
}

TEST_IMPL(pipe_stdio_no_keep_alive_win) {
  uv_file out_fds[2];
  uv_file err_fds[2];
  int saved_out = -1;
  int saved_err = -1;
  uv_pipe_t pipe_out;
  uv_pipe_t pipe_err;
  uv_write_t wr_out;
  uv_write_t wr_err;
  uv_buf_t buf_out;
  uv_buf_t buf_err;
  uv_fs_t stat_req;

  uv_timer_t watchdog;

  if (!is_win2025_or_newer()) {
    RETURN_SKIP("Test requires Windows Server 2025 (build >= 26100).");
  }

  /* Overlapped stdio pipes on fd 1 and fd 2 (UV_NONBLOCK_PIPE =>
   * FILE_FLAG_OVERLAPPED) so uv_pipe_open takes the IOCP-attach path we
   * suspect is involved in #5147 (root cause unconfirmed). fd <= 2 makes
   * uv_pipe_open use the stdio code path (0 <= file <= 2). */
  open_stdio_pipe(&pipe_out, out_fds, 1, &saved_out);
  open_stdio_pipe(&pipe_err, err_fds, 2, &saved_err);

  /* Restore the real console fds immediately so TAP output and the diagnostic
   * dump below land in the runner's captured stdout, not the manufactured
   * pipes. uv_pipe_open duplicated the underlying handles, so we can drop our
   * own write-end fds now; the read ends (fds[0]) stay open as the peers. */
  _dup2(saved_out, 1);
  _close(saved_out);
  _close(out_fds[1]);
  _dup2(saved_err, 2);
  _close(saved_err);
  _close(err_fds[1]);

  /* Write to both pipes (like console.log / console.error) and leave them
   * open. The few bytes fit the pipe buffer, so the writes complete without a
   * reader. */
  buf_out = uv_buf_init("out\n", 4);
  ASSERT_OK(uv_write(&wr_out, (uv_stream_t*) &pipe_out, &buf_out, 1,
                     noop_write_cb));
  buf_err = uv_buf_init("err\n", 4);
  ASSERT_OK(uv_write(&wr_err, (uv_stream_t*) &pipe_err, &buf_err, 1,
                     noop_write_cb));

  /* In-flight async work that completes through the threadpool -> loop IOCP,
   * mirroring the Node repro's https requests. */
  ASSERT_OK(uv_fs_stat(uv_default_loop(), &stat_req, ".", stat_cb));

  uv__loop_debug_dump(uv_default_loop(),
                      "reproducer A: before run (2 stdio pipes + writes + fs)",
                      stdout);

  /* Unref'd watchdog: fires only if something else keeps the loop alive. */
  watchdog_fired = 0;
  ASSERT_OK(uv_timer_init(uv_default_loop(), &watchdog));
  ASSERT_OK(uv_timer_start(&watchdog, watchdog_cb, 5000, 0));
  uv_unref((uv_handle_t*) &watchdog);

  /* Healthy: returns once the writes + fs request complete (idle pipes do not
   * keep the loop alive). Win2025 (workaround off): stays alive until the
   * watchdog fires. */
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  uv__loop_debug_dump(uv_default_loop(), "reproducer A: after run", stdout);

  /* Canary: on Win2025 with the workaround disabled this fails because the
   * loop did not drain. This assertion is the permanent contract; when the
   * fix lands, what changes is the #if 0 workaround in src/win/pipe.c and the
   * Win2025-only skip guard, not this assert. */
  ASSERT_OK(watchdog_fired);

  uv_close((uv_handle_t*) &watchdog, NULL);
  uv_close((uv_handle_t*) &pipe_out, NULL);
  uv_close((uv_handle_t*) &pipe_err, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  _close(out_fds[0]);
  _close(err_fds[0]);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else /* _WIN32 */

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif /* _WIN32 */
