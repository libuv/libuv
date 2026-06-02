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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Reproducer B (cross-process, faithful) for
 * https://github.com/libuv/libuv/issues/5147.
 *
 * Mirrors the shell -> node shape: the parent spawns a child whose stdout is an
 * overlapped pipe the parent holds the read end of (UV_CREATE_PIPE |
 * UV_WRITABLE_PIPE | UV_NONBLOCK_PIPE -- the nonblock flag makes the child's
 * inherited fd 1 overlapped so uv_pipe_open associates it with the loop's
 * IOCP, the path we suspect is involved in #5147 (root cause unconfirmed)).
 * The child calls uv_pipe_open(1), writes a few bytes (like console.log),
 * leaves the pipe open, and runs its loop:
 *
 *   - Healthy OS: the idle stdout pipe does not keep the child loop alive, the
 *     child exits 0, its write end closes, the parent reads EOF and drains.
 *   - Win2025 (#5147, workaround disabled): the child loop never drains. The
 *     child's own unref'd watchdog (8s) fires, dumps child-side loop
 *     diagnostics to inherited stderr (-> CI log), and exits 99. The parent
 *     observes a non-zero exit status -> assertion FAILS (intended canary).
 *
 * A parent-side kill watchdog (15s) is a backstop in case the child wedges so
 * hard it cannot run its own watchdog; it fires before the 30s runner budget.
 * Skipped on Windows older than 26100.
 */

/* TEMPORARY: revert before merge -- #5147. Defined in src/win/core.c. */
void uv__loop_debug_dump(uv_loop_t* loop, const char* tag);

#define CHILD_HELPER_NAME "pipe_stdio_loop_alive_helper_win"
#define CHILD_DRAIN_TIMEOUT_MS 8000
#define PARENT_KILL_TIMEOUT_MS 15000
#define CHILD_STUCK_EXIT_CODE 99

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

/* ----- child helper (runs in the spawned process) ----- */

static int child_watchdog_fired;

static void child_watchdog_cb(uv_timer_t* handle) {
  child_watchdog_fired = 1;
  uv__loop_debug_dump(handle->loop, "reproducer B child: loop still alive");
  uv_stop(handle->loop);
}

static void child_write_cb(uv_write_t* req, int status) {
  (void) req;
  if (status != 0) {
    fprintf(stderr, "child: uv_write completed with error: %s\n",
            uv_strerror(status));
    fflush(stderr);
  }
  /* Intentionally leave the stdout pipe open, like Node's process.stdout. */
}

int pipe_stdio_loop_alive_helper_win(void) {
  uv_loop_t* loop;
  uv_pipe_t stdout_pipe;
  uv_write_t write_req;
  uv_buf_t buf;
  uv_timer_t watchdog;
  int err;

  loop = uv_default_loop();

  err = uv_pipe_init(loop, &stdout_pipe, 0);
  if (err != 0) {
    fprintf(stderr, "child: uv_pipe_init failed: %s\n", uv_strerror(err));
    fflush(stderr);
    return 2;
  }
  err = uv_pipe_open(&stdout_pipe, 1);
  if (err != 0) {
    fprintf(stderr, "child: uv_pipe_open(1) failed: %s\n", uv_strerror(err));
    fflush(stderr);
    return 3;
  }

  buf = uv_buf_init("hi\n", 3);
  err = uv_write(&write_req, (uv_stream_t*) &stdout_pipe, &buf, 1, child_write_cb);
  if (err != 0) {
    fprintf(stderr, "child: uv_write failed: %s\n", uv_strerror(err));
    fflush(stderr);
    return 4;
  }

  child_watchdog_fired = 0;
  err = uv_timer_init(loop, &watchdog);
  if (err != 0) {
    fprintf(stderr, "child: uv_timer_init failed: %s\n", uv_strerror(err));
    fflush(stderr);
    return 5;
  }
  err = uv_timer_start(&watchdog, child_watchdog_cb, CHILD_DRAIN_TIMEOUT_MS, 0);
  if (err != 0) {
    fprintf(stderr, "child: uv_timer_start failed: %s\n", uv_strerror(err));
    fflush(stderr);
    return 6;
  }
  uv_unref((uv_handle_t*) &watchdog);

  uv_run(loop, UV_RUN_DEFAULT);

  /* If the watchdog fired the loop failed to drain (the #5147 hang). */
  return child_watchdog_fired ? CHILD_STUCK_EXIT_CODE : 0;
}

/* ----- parent test ----- */

static uv_process_t process;
static uv_process_options_t options;
static uv_pipe_t child_stdout;
static char child_output[1024];
static size_t child_output_used;
static int exit_cb_called;
static int64_t child_exit_status;
static int child_term_signal;
static int kill_watchdog_fired;
static char exepath[1024];
static size_t exepath_size = sizeof(exepath);
static char* args[3];

static void alloc_cb(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
  (void) handle;
  (void) suggested;
  if (child_output_used >= sizeof(child_output)) {
    buf->base = NULL;
    buf->len = 0;
    return;
  }
  buf->base = child_output + child_output_used;
  buf->len = (unsigned long) (sizeof(child_output) - child_output_used);
}

static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  (void) buf;
  if (nread > 0) {
    child_output_used += (size_t) nread;
  } else if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "parent: unexpected read error: %s\n",
              uv_strerror((int) nread));
      fflush(stderr);
    }
    uv_close((uv_handle_t*) stream, NULL);
  }
}

static void exit_cb(uv_process_t* proc, int64_t exit_status, int term_signal) {
  exit_cb_called++;
  child_exit_status = exit_status;
  child_term_signal = term_signal;
  uv_close((uv_handle_t*) proc, NULL);
}

static void kill_watchdog_cb(uv_timer_t* handle) {
  int err;
  kill_watchdog_fired = 1;
  uv__loop_debug_dump(handle->loop, "reproducer B parent: child did not exit");
  err = uv_process_kill(&process, SIGTERM);
  if (err != 0) {
    fprintf(stderr, "parent: uv_process_kill failed: %s\n", uv_strerror(err));
    fflush(stderr);
  }
}

TEST_IMPL(pipe_stdio_loop_alive_spawn_win) {
  uv_stdio_container_t stdio[3];
  uv_timer_t kill_watchdog;
  int r;

  if (!is_win2025_or_newer()) {
    RETURN_SKIP("Test requires Windows Server 2025 (build >= 26100).");
  }

  r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = CHILD_HELPER_NAME;
  args[2] = NULL;

  memset(&options, 0, sizeof(options));
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &child_stdout, 0));
  stdio[0].flags = UV_IGNORE;
  stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE | UV_NONBLOCK_PIPE;
  stdio[1].data.stream = (uv_stream_t*) &child_stdout;
  stdio[2].flags = UV_INHERIT_FD;     /* child diagnostics -> our stderr -> CI log */
  stdio[2].data.fd = 2;
  options.stdio = stdio;
  options.stdio_count = 3;

  exit_cb_called = 0;
  child_exit_status = -1;
  child_term_signal = -1;
  kill_watchdog_fired = 0;
  child_output_used = 0;

  r = uv_spawn(uv_default_loop(), &process, &options);
  if (r != 0) {
    fprintf(stderr, "reproducer B: uv_spawn failed: %s\n", uv_strerror(r));
    fflush(stderr);
  }
  ASSERT_OK(r);

  /* Parent holds and drains the read end, like a shell consuming stdout. */
  ASSERT_OK(uv_read_start((uv_stream_t*) &child_stdout, alloc_cb, read_cb));

  ASSERT_OK(uv_timer_init(uv_default_loop(), &kill_watchdog));
  ASSERT_OK(uv_timer_start(&kill_watchdog, kill_watchdog_cb,
                           PARENT_KILL_TIMEOUT_MS, 0));
  uv_unref((uv_handle_t*) &kill_watchdog);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, exit_cb_called);

  /* Canary: on Win2025 with the workaround disabled the child's loop hangs,
   * its watchdog fires, and it exits CHILD_STUCK_EXIT_CODE (or the parent had
   * to kill it). Re-tighten / remove when the fix lands. */
  ASSERT_EQ(0, kill_watchdog_fired);
  ASSERT_EQ(0, child_exit_status);
  ASSERT_EQ(0, child_term_signal);

  uv_close((uv_handle_t*) &kill_watchdog, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else /* _WIN32 */

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif /* _WIN32 */
