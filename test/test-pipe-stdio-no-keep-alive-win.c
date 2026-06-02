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
 * Minimal single/dual overlapped stdio-pipe variants drained cleanly on the
 * Win2025 CI runner, so this version mirrors the hanging Node.js repro as
 * closely as is practical in-process:
 *   - BOTH stdout (fd 1) and stderr (fd 2) are overlapped stdio pipes, written
 *     to (like console.log / console.error) and left open;
 *   - a real loopback TCP round-trip (connect -> 1 byte -> echo -> close) runs
 *     to completion, so genuine socket IOCP completions interleave with the
 *     stdio-pipe IOCP, standing in for the https requests the Node repro makes.
 *
 * The loop is driven with uv_run(UV_RUN_DEFAULT) under an uv_unref'd watchdog:
 *   - Healthy OS: everything completes, the idle stdio pipes do not keep the
 *     loop alive, uv_run returns before the watchdog fires -> PASS.
 *   - Win2025 with the workaround disabled (#5147): if the residual ref the
 *     issue describes is present, the loop stays alive, the watchdog fires,
 *     dumps loop state and stops the loop -> the assertion FAILS (canary).
 *
 * uv__loop_debug_dump is routed to stdout (fd 1/2 are restored to the console
 * before any dump). The test entry is registered with show_output=1 so the
 * runner echoes this stdout into the CI log on both pass and fail.
 *
 * Skipped on Windows older than build 26100 (bug not present).
 */

/* TEMPORARY: revert before merge — #5147. Defined in src/win/core.c and
 * UV_EXTERN-exported there so this shared-lib-linked test can resolve it. */
UV_EXTERN void uv__loop_debug_dump(uv_loop_t* loop,
                                   const char* tag,
                                   FILE* stream);

static int watchdog_fired;

/* TCP echo state. */
static uv_tcp_t tcp_server;
static uv_tcp_t tcp_client;
static uv_tcp_t tcp_accepted;
static uv_write_t tcp_write_req;
static char tcp_slab[64];

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
  /* Intentionally leave the stdio pipe open, like Node's process.stdout. */
}

static void tcp_close_cb(uv_handle_t* handle) {
  (void) handle;
}

static void tcp_alloc_cb(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
  (void) handle;
  (void) suggested;
  buf->base = tcp_slab;
  buf->len = (unsigned long) sizeof(tcp_slab);
}

static void tcp_server_read_cb(uv_stream_t* stream,
                               ssize_t nread,
                               const uv_buf_t* buf) {
  (void) stream;
  (void) nread;
  (void) buf;
  /* read_start delivers the data callback and then UV_EOF; only tear down
   * once (a second uv_close on a closing handle aborts in debug builds). */
  if (uv_is_closing((uv_handle_t*) &tcp_accepted))
    return;
  uv_close((uv_handle_t*) &tcp_accepted, tcp_close_cb);
  uv_close((uv_handle_t*) &tcp_server, tcp_close_cb);
}

static void tcp_connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(server->loop, &tcp_accepted));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &tcp_accepted));
  ASSERT_OK(uv_read_start((uv_stream_t*) &tcp_accepted,
                          tcp_alloc_cb,
                          tcp_server_read_cb));
}

static void tcp_client_write_cb(uv_write_t* req, int status) {
  (void) req;
  ASSERT_OK(status);
  uv_close((uv_handle_t*) &tcp_client, tcp_close_cb);
}

static void tcp_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t b;
  (void) req;
  ASSERT_OK(status);
  b = uv_buf_init("x", 1);
  ASSERT_OK(uv_write(&tcp_write_req, (uv_stream_t*) &tcp_client, &b, 1,
                     tcp_client_write_cb));
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
  struct sockaddr_in listen_addr;
  struct sockaddr_in connect_addr;
  uv_connect_t connect_req;
  int namelen;
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

  /* Real loopback TCP round-trip, standing in for the Node repro's https
   * requests: bind+listen on an ephemeral port, connect, send one byte, the
   * server reads it, then both sides close. Genuine socket IOCP completions
   * interleave with the stdio-pipe IOCP. */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", 0, &listen_addr));
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &tcp_server));
  ASSERT_OK(uv_tcp_bind(&tcp_server, (const struct sockaddr*) &listen_addr, 0));
  namelen = sizeof(connect_addr);
  ASSERT_OK(uv_tcp_getsockname(&tcp_server,
                               (struct sockaddr*) &connect_addr,
                               &namelen));
  ASSERT_OK(uv_listen((uv_stream_t*) &tcp_server, 1, tcp_connection_cb));
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &tcp_client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &tcp_client,
                           (const struct sockaddr*) &connect_addr,
                           tcp_connect_cb));

  uv__loop_debug_dump(uv_default_loop(),
                      "reproducer A: before run (2 stdio pipes + writes + tcp)",
                      stdout);

  /* Unref'd watchdog: fires only if something else keeps the loop alive. */
  watchdog_fired = 0;
  ASSERT_OK(uv_timer_init(uv_default_loop(), &watchdog));
  ASSERT_OK(uv_timer_start(&watchdog, watchdog_cb, 5000, 0));
  uv_unref((uv_handle_t*) &watchdog);

  /* Healthy: returns once the writes + TCP round-trip complete (idle stdio
   * pipes do not keep the loop alive). Win2025 (workaround off): stays alive
   * until the watchdog fires. */
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
