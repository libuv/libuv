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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


TEST_IMPL(pipe_init_ex) {
  uv_loop_t loop;
  uv_pipe_t pipe;

  ASSERT_OK(uv_loop_init(&loop));
  memset(&pipe, 0xAA, sizeof(pipe));
  ASSERT_EQ(UV_EINVAL, uv_pipe_init_ex(&loop, &pipe, 4));
  ASSERT_OK(uv_loop_close(&loop));

  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_pipe_init_ex(&loop, &pipe, UV_PIPE_INIT_IPC));
  ASSERT_EQ(UV_NAMED_PIPE, uv_handle_get_type((uv_handle_t*) &pipe));
  ASSERT_EQ(1, pipe.ipc);
  uv_close((uv_handle_t*) &pipe, NULL);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));

  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_pipe_init(&loop, &pipe, -1));
  ASSERT_EQ(-1, pipe.ipc);
  uv_close((uv_handle_t*) &pipe, NULL);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));
#ifndef _WIN32
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_pipe_init_ex(&loop, &pipe, UV_PIPE_INIT_UNIX_SOCKET |
                                       UV_PIPE_INIT_IPC));
  ASSERT_EQ(1, pipe.ipc);
  uv_close((uv_handle_t*) &pipe, NULL);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));
#endif
  return 0;
}


#ifdef _WIN32

static uv_pipe_t server;
static uv_pipe_t client;
static uv_pipe_t peer;
static uv_connect_t connect_req;
static uv_connect_t connect_again_req;
static int connect_again_count;
static uv_write_t client_write;
static uv_write_t peer_write;
static uv_shutdown_t client_shutdown;
static uv_shutdown_t peer_shutdown;
static char endpoint[256];
static char received[2][4];
static size_t received_len[2];
static int close_count;
static int connect_count;
static int write_count;
static int shutdown_count;
static int eof_count;
static int expected_connect_error;
static int check_connect_close_order;


static void make_endpoint(void) {
  snprintf(endpoint, sizeof(endpoint), "uv-uds-%lu.sock",
           (unsigned long) GetCurrentProcessId());
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
}


static int init_unix_socket(uv_pipe_t* pipe) {
  int r;

  r = uv_pipe_init_ex(uv_default_loop(), pipe, UV_PIPE_INIT_UNIX_SOCKET);
  if (r != UV_EAFNOSUPPORT)
    ASSERT_OK(r);
  return r;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  if (check_connect_close_order && handle == (uv_handle_t*) &client)
    ASSERT_EQ(1, connect_count);
  close_count++;
}


static void check_name(uv_pipe_t* pipe, int remote, const char* expected) {
  char buf[256];
  size_t len;
  size_t expected_len;
  int (*getname)(const uv_pipe_t*, char*, size_t*);

  getname = remote ? uv_pipe_getpeername : uv_pipe_getsockname;
  expected_len = strlen(expected);
  len = sizeof(buf);
  ASSERT_OK(getname(pipe, buf, &len));
  ASSERT_EQ(expected_len, len);
  ASSERT_MEM_EQ(expected, buf, len);
  ASSERT_EQ('\0', buf[len]);

  if (expected_len != 0) {
    len = expected_len;
    ASSERT_EQ(UV_ENOBUFS, getname(pipe, buf, &len));
    ASSERT_EQ(expected_len + 1, len);
    ASSERT_OK(getname(pipe, buf, &len));
    ASSERT_EQ(expected_len, len);
  }
  len = 0;
  ASSERT_EQ(UV_EINVAL, getname(pipe, buf, &len));
  len = sizeof(buf);
  ASSERT_EQ(UV_EINVAL, getname(pipe, NULL, &len));
  ASSERT_EQ(UV_EINVAL, getname(pipe, buf, NULL));
}


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  ASSERT_NOT_NULL(handle);
  *buf = uv_buf_init(malloc(64), 64);
  ASSERT_NOT_NULL(buf->base);
}


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_OK(status);
  shutdown_count++;
  if (req->handle == (uv_stream_t*) &peer)
    uv_close((uv_handle_t*) &peer, close_cb);
}


static void write_cb(uv_write_t* req, int status) {
  uv_shutdown_t* shutdown_req;

  ASSERT_OK(status);
  write_count++;
  shutdown_req = req->handle == (uv_stream_t*) &client
               ? &client_shutdown : &peer_shutdown;
  ASSERT_OK(uv_shutdown(shutdown_req, req->handle, shutdown_cb));
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  uv_buf_t reply;
  int index;

  index = stream == (uv_stream_t*) &client ? 0 : 1;
  if (nread > 0) {
    ASSERT_LE(received_len[index] + nread, sizeof(received[index]));
    memcpy(received[index] + received_len[index], buf->base, nread);
    received_len[index] += nread;
  } else if (nread < 0) {
    ASSERT_EQ(UV_EOF, nread);
    ASSERT_EQ(4, received_len[index]);
    eof_count++;
    if (index == 1) {
      ASSERT_MEM_EQ("ping", received[1], 4);
      reply = uv_buf_init("pong", 4);
      ASSERT_OK(uv_write(&peer_write, stream, &reply, 1, write_cb));
    } else {
      ASSERT_MEM_EQ("pong", received[0], 4);
      uv_close((uv_handle_t*) &client, close_cb);
      uv_close((uv_handle_t*) &server, close_cb);
    }
  }
  free(buf->base);
}


static void connection_cb(uv_stream_t* stream, int status) {
  static uv_pipe_t wrong_mode;
  uv_os_fd_t fd;
  DWORD flags;

  ASSERT_OK(status);
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &wrong_mode, 0));
  ASSERT_EQ(UV_EINVAL, uv_accept(stream, (uv_stream_t*) &wrong_mode));
  uv_close((uv_handle_t*) &wrong_mode, NULL);
  ASSERT_OK(init_unix_socket(&peer));
  ASSERT_OK(uv_accept(stream, (uv_stream_t*) &peer));
  check_name(&peer, 0, endpoint);
  check_name(&peer, 1, "");
  ASSERT_OK(uv_fileno((uv_handle_t*) &peer, &fd));
  ASSERT(GetHandleInformation(fd, &flags));
  ASSERT_EQ(0, flags & HANDLE_FLAG_INHERIT);
  ASSERT_EQ(UV_ENOTSUP, uv_stream_set_blocking((uv_stream_t*) &peer, 1));
  ASSERT_OK(uv_read_start((uv_stream_t*) &peer, alloc_cb, read_cb));
}


static void check_stdio(uv_pipe_t* pipe, uv_stdio_flags flags) {
  static uv_process_t processes[2];
  static int process_count;
  uv_process_options_t options;
  uv_stdio_container_t stdio;
  char exepath[1024];
  char* args[3];
  size_t len;

  ASSERT_LT(process_count, 2);
  memset(&options, 0, sizeof(options));
  len = sizeof(exepath);
  ASSERT_OK(uv_exepath(exepath, &len));
  args[0] = exepath;
  args[1] = "spawn_helper1";
  args[2] = NULL;
  stdio.flags = flags;
  stdio.data.stream = (uv_stream_t*) pipe;
  options.file = exepath;
  options.args = args;
  options.stdio = &stdio;
  options.stdio_count = 1;
  ASSERT_EQ(UV_ENOTSUP, uv_spawn(uv_default_loop(),
                               &processes[process_count], &options));
  uv_close((uv_handle_t*) &processes[process_count++], NULL);
}


static void connect_again_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &client);
  ASSERT_EQ(UV_EBUSY, status);
  connect_again_count++;
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;

  ASSERT_OK(status);
  connect_count++;
  check_name(&client, 0, "");
  check_name(&client, 1, endpoint);
  check_stdio(&client, UV_INHERIT_STREAM);
  ASSERT_EQ(UV_EBUSY, uv_pipe_connect2(&connect_again_req, &client, endpoint,
                                      strlen(endpoint), 0, connect_again_cb));
  uv_pipe_connect(&connect_again_req, &client, endpoint, connect_again_cb);
  ASSERT_OK(uv_read_start((uv_stream_t*) &client, alloc_cb, read_cb));
  ASSERT_OK(uv_read_stop((uv_stream_t*) &client));
  ASSERT_OK(uv_read_start((uv_stream_t*) &client, alloc_cb, read_cb));
  buf = uv_buf_init("pi", 2);
  ASSERT_EQ(2, uv_try_write(req->handle, &buf, 1));
  buf = uv_buf_init("ng", 2);
  ASSERT_OK(uv_write(&client_write, req->handle, &buf, 1, write_cb));
}


TEST_IMPL(pipe_unix_socket) {
  char buf[256];
  size_t len;

  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  ASSERT_OK(init_unix_socket(&client));
  uv_pipe_pending_instances(&server, 16);
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  check_name(&server, 0, endpoint);
  len = sizeof(buf);
  ASSERT_EQ(UV_ENOTCONN, uv_pipe_getpeername(&server, buf, &len));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 16, connection_cb));
  ASSERT_OK(uv_pipe_connect2(&connect_req, &client, endpoint,
                            strlen(endpoint), 0, connect_cb));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_count);
  ASSERT_EQ(1, connect_again_count);
  ASSERT_EQ(2, write_count);
  ASSERT_EQ(2, shutdown_count);
  ASSERT_EQ(2, eof_count);
  ASSERT_EQ(3, close_count);
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void error_connect_cb(uv_connect_t* req, int status) {
  if (expected_connect_error != 0)
    ASSERT_EQ(expected_connect_error, status);
  else
    ASSERT_LT(status, 0);
  connect_count++;
  if (!uv_is_closing((uv_handle_t*) req->handle))
    uv_close((uv_handle_t*) req->handle, close_cb);
}


TEST_IMPL(pipe_unix_socket_errors) {
  uv_pipe_t duplicate;
  uv_file fds[2];
  uv_fs_t fs_req;
  char long_path[109];
  char nul_path[] = "uv-uds\0invalid";
  char missing_parent[256];
  FILE* file;

  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  memset(long_path, 'x', sizeof(long_path));
  long_path[sizeof(long_path) - 1] = '\0';
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&server, endpoint,
                                   strlen(endpoint), 2));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&server, NULL, 1, 0));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind(&server, ""));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&server, nul_path,
                                   sizeof(nul_path) - 1, 0));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind(&server, long_path));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&server, long_path, 108,
                                   UV_PIPE_NO_TRUNCATE));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind(&server, "\\\\.\\pipe\\uv-uds"));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind(&server, "\\\\?\\pipe\\uv-uds"));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&server, "\0abstract", 9, 0));

  check_stdio(&server, UV_CREATE_PIPE | UV_READABLE_PIPE);
  ASSERT_OK(uv_pipe(fds, 0, 0));
  ASSERT_EQ(UV_ENOTSUP, uv_pipe_open(&server, fds[0]));
  ASSERT_OK(uv_fs_close(NULL, &fs_req, fds[0], NULL));
  uv_fs_req_cleanup(&fs_req);
  ASSERT_OK(uv_fs_close(NULL, &fs_req, fds[1], NULL));
  uv_fs_req_cleanup(&fs_req);

  snprintf(missing_parent, sizeof(missing_parent), "%s/missing", endpoint);
  ASSERT_LT(uv_pipe_bind(&server, missing_parent), 0);
  file = fopen(endpoint, "w");
  ASSERT_NOT_NULL(file);
  ASSERT_GE(fputs("sentinel", file), 0);
  ASSERT_OK(fclose(file));
  ASSERT_LT(uv_pipe_bind(&server, endpoint), 0);
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_NE(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  ASSERT_OK(remove(endpoint));

  ASSERT_OK(init_unix_socket(&server));
  ASSERT_OK(init_unix_socket(&duplicate));
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  ASSERT_EQ(UV_ENOTSUP, uv_pipe_chmod(&server, UV_READABLE));
  ASSERT_EQ(UV_EADDRINUSE, uv_pipe_bind(&duplicate, endpoint));
  uv_close((uv_handle_t*) &duplicate, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_NOWAIT));
  ASSERT_NE(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));

  ASSERT_OK(init_unix_socket(&client));
  ASSERT_EQ(UV_EINVAL, uv_pipe_connect2(&connect_req, &client, long_path,
                                      108, 0, error_connect_cb));
  ASSERT_EQ(UV_EINVAL, uv_pipe_connect2(&connect_req, &client, nul_path,
                                      sizeof(nul_path) - 1, 0,
                                      error_connect_cb));
  ASSERT_EQ(0, connect_count);
  expected_connect_error = UV_EINVAL;
  uv_pipe_connect(&connect_req, &client, long_path, error_connect_cb);
  ASSERT_EQ(0, connect_count);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_count);

  ASSERT_OK(init_unix_socket(&client));
  expected_connect_error = 0;
  ASSERT_OK(uv_pipe_connect2(&connect_req, &client, endpoint,
                            strlen(endpoint), 0, error_connect_cb));
  ASSERT_EQ(1, connect_count);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(2, connect_count);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_unix_socket_cleanup) {
  char cwd[256];
  char relative[64];
  size_t len;

  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  snprintf(relative, sizeof(relative), "uv-uds-%lu.sock",
           (unsigned long) GetCurrentProcessId());
  ASSERT_OK(uv_pipe_bind(&server, relative));
  len = sizeof(cwd);
  ASSERT_OK(uv_cwd(cwd, &len));
  ASSERT_OK(uv_chdir(".."));
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(uv_chdir(cwd));
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  ASSERT_OK(init_unix_socket(&server));
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(2, close_count);
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void unexpected_connection_cb(uv_stream_t* stream, int status) {
  ASSERT(0 && "connection callback after server close");
}


TEST_IMPL(pipe_unix_socket_close_connect) {
  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  ASSERT_OK(init_unix_socket(&client));
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 16, unexpected_connection_cb));
  expected_connect_error = UV_ECANCELED;
  check_connect_close_order = 1;
  ASSERT_OK(uv_pipe_connect2(&connect_req, &client, endpoint,
                            strlen(endpoint), 0, error_connect_cb));
  uv_close((uv_handle_t*) &client, close_cb);
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_count);
  ASSERT_EQ(2, close_count);
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_unix_socket_version) {
  uv_loop_t loop;
  uv_pipe_t pipe;
  SOCKET sock;
  int r;
  int err;

  ASSERT_OK(uv_loop_init(&loop));
  /* Probe the provider, not the Windows marketing version or SDK headers. */
  sock = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0,
                   WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  err = sock == INVALID_SOCKET ? WSAGetLastError() : 0;
  r = uv_pipe_init_ex(&loop, &pipe, UV_PIPE_INIT_UNIX_SOCKET);
  if (sock == INVALID_SOCKET) {
    ASSERT_EQ(WSAEAFNOSUPPORT, err);
    ASSERT_EQ(UV_EAFNOSUPPORT, r);
  } else {
    ASSERT_OK(closesocket(sock));
    ASSERT_OK(r);
    uv_close((uv_handle_t*) &pipe, NULL);
    ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  }
  ASSERT_EQ(UV_ENOTSUP, uv_pipe_init_ex(&loop, &pipe,
                                      UV_PIPE_INIT_IPC |
                                      UV_PIPE_INIT_UNIX_SOCKET));
  ASSERT_OK(uv_pipe_init(&loop, &pipe, 0));
  ASSERT_EQ(UV_EACCES, uv_pipe_bind(&pipe, "bad-pipe"));
  ASSERT_OK(uv_pipe_bind(&pipe, TEST_PIPENAME));
  uv_close((uv_handle_t*) &pipe, NULL);
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));
  return 0;
}


#define CONNECTIONS 8
static uv_pipe_t clients[CONNECTIONS];
static uv_pipe_t peers[CONNECTIONS];
static uv_connect_t connections[CONNECTIONS];
static int accept_count;


static void pending_connect_cb(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  connect_count++;
  uv_close((uv_handle_t*) req->handle, close_cb);
}


static void pending_connection_cb(uv_stream_t* stream, int status) {
  ASSERT_OK(status);
  ASSERT_LT(accept_count, CONNECTIONS);
  ASSERT_OK(init_unix_socket(&peers[accept_count]));
  ASSERT_OK(uv_accept(stream, (uv_stream_t*) &peers[accept_count]));
  uv_close((uv_handle_t*) &peers[accept_count], close_cb);
  if (++accept_count == CONNECTIONS)
    uv_close((uv_handle_t*) &server, close_cb);
}


TEST_IMPL(pipe_unix_socket_pending) {
  int i;

  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, CONNECTIONS,
                     pending_connection_cb));
  for (i = 0; i < CONNECTIONS; i++) {
    ASSERT_OK(init_unix_socket(&clients[i]));
    ASSERT_OK(uv_pipe_connect2(&connections[i], &clients[i], endpoint,
                              strlen(endpoint), 0, pending_connect_cb));
  }
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(CONNECTIONS, connect_count);
  ASSERT_EQ(CONNECTIONS, accept_count);
  ASSERT_EQ(CONNECTIONS * 2 + 1, close_count);
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_unix_socket_paths) {
  uv_fs_t req;
  char path[108];
  WCHAR wpath[108];
  size_t len;

  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  len = strlen(endpoint);
  memcpy(path, endpoint, len);
  memset(path + len, 'x', sizeof(path) - len - 1);
  path[sizeof(path) - 1] = '\0';
  ASSERT_OK(uv_pipe_bind2(&server, path, 107, UV_PIPE_NO_TRUNCATE));
  check_name(&server, 0, path);
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(UV_ENOENT, uv_fs_lstat(NULL, &req, path, NULL));
  uv_fs_req_cleanup(&req);

  ASSERT_OK(init_unix_socket(&server));
  ASSERT_LT(len + 3, sizeof(path));
  memcpy(path, endpoint, len);
  memcpy(path + len, "-\xc3\xa9", 4);
  ASSERT_OK(uv_pipe_bind(&server, path));
  check_name(&server, 0, path);
  ASSERT_NE(0, MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 108));
  ASSERT_NE(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(wpath));
  uv_close((uv_handle_t*) &server, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(UV_ENOENT, uv_fs_lstat(NULL, &req, path, NULL));
  uv_fs_req_cleanup(&req);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static uv_write_t pending_writes[8];
static char pending_data[1024 * 1024];
static int pending_ready;
static int canceled_writes;


static void pending_write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &client);
  if (status != 0)
    ASSERT_EQ(UV_ECANCELED, status);
  if (status == UV_ECANCELED)
    canceled_writes++;
  write_count++;
}


static void pending_write_close_cb(uv_handle_t* handle) {
  ASSERT_PTR_EQ(handle, (uv_handle_t*) &client);
  ASSERT_EQ(8, write_count);
  ASSERT_GT(canceled_writes, 0);
  close_count++;
  uv_close((uv_handle_t*) &peer, close_cb);
}


static void start_pending_writes(void) {
  uv_buf_t buf;
  int i;

  if (++pending_ready != 2)
    return;
  buf = uv_buf_init(pending_data, sizeof(pending_data));
  for (i = 0; i < 8; i++)
    ASSERT_OK(uv_write(&pending_writes[i], (uv_stream_t*) &client,
                       &buf, 1, pending_write_cb));
  ASSERT_GT(uv_stream_get_write_queue_size((uv_stream_t*) &client), 0);
  uv_close((uv_handle_t*) &client, pending_write_close_cb);
}


static void pending_write_connect_cb(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  connect_count++;
  start_pending_writes();
}


static void pending_write_connection_cb(uv_stream_t* stream, int status) {
  ASSERT_OK(status);
  ASSERT_OK(init_unix_socket(&peer));
  ASSERT_OK(uv_accept(stream, (uv_stream_t*) &peer));
  uv_close((uv_handle_t*) &server, close_cb);
  start_pending_writes();
}


TEST_IMPL(pipe_unix_socket_close_write) {
  if (init_unix_socket(&server) == UV_EAFNOSUPPORT)
    RETURN_SKIP("AF_UNIX is not supported by this Windows socket provider");
  make_endpoint();
  ASSERT_OK(init_unix_socket(&client));
  ASSERT_OK(uv_pipe_bind(&server, endpoint));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 16,
                     pending_write_connection_cb));
  ASSERT_OK(uv_pipe_connect2(&connect_req, &client, endpoint,
                            strlen(endpoint), 0, pending_write_connect_cb));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_count);
  ASSERT_EQ(8, write_count);
  ASSERT_EQ(3, close_count);
  ASSERT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesA(endpoint));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif  /* _WIN32 */
