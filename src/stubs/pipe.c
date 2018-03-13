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

int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc) {
	return UV_ENOSYS;
}

int uv_stdio_pipe_server(uv_loop_t* loop, uv_pipe_t* handle, DWORD access,
    char* name, size_t nameSize) {
	return UV_ENOSYS;
}

void uv_pipe_endgame(uv_loop_t* loop, uv_pipe_t* handle) {
}


void uv_pipe_pending_instances(uv_pipe_t* handle, int count) {
}

/* Creates a pipe server. */
int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
	return UV_ENOSYS;
}


void uv_pipe_connect(uv_connect_t* req, uv_pipe_t* handle,
    const char* name, uv_connect_cb cb) {
}


void uv__pipe_pause_read(uv_pipe_t* handle) {
}


void uv__pipe_unpause_read(uv_pipe_t* handle) {
}


void uv__pipe_stop_read(uv_pipe_t* handle) {
}

void uv_pipe_cleanup(uv_loop_t* loop, uv_pipe_t* handle) {
}


void uv_pipe_close(uv_loop_t* loop, uv_pipe_t* handle) {
}

int uv_pipe_accept(uv_pipe_t* server, uv_stream_t* client) {
	return UV_ENOSYS;
}


/* Starts listening for connections for the given pipe. */
int uv_pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb) {
	return UV_ENOSYS;
}

int uv_pipe_read_start(uv_pipe_t* handle,
                       uv_alloc_cb alloc_cb,
                       uv_read_cb read_cb) {
	return UV_ENOSYS;
}

int uv_pipe_write(uv_loop_t* loop,
                  uv_write_t* req,
                  uv_pipe_t* handle,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  uv_write_cb cb) {
	return UV_ENOSYS;
}


int uv_pipe_write2(uv_loop_t* loop,
                   uv_write_t* req,
                   uv_pipe_t* handle,
                   const uv_buf_t bufs[],
                   unsigned int nbufs,
                   uv_stream_t* send_handle,
                   uv_write_cb cb) {
	return UV_ENOSYS;
}


void uv_process_pipe_read_req(uv_loop_t* loop, uv_pipe_t* handle, uv_req_t* req) {
}


void uv_process_pipe_write_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_write_t* req) {
}


void uv_process_pipe_accept_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_req_t* raw_req) {
}


void uv_process_pipe_connect_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_connect_t* req) {
}


void uv_process_pipe_shutdown_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_shutdown_t* req) {
}

int uv_pipe_open(uv_pipe_t* pipe, uv_os_fd_t os_handle) {
	return UV_ENOSYS;
}

int uv_pipe_pending_count(uv_pipe_t* handle) {
	return UV_ENOSYS;
}

int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size) {
	return UV_ENOSYS;
}

int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size) {
	return UV_ENOSYS;
}

uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle) {
	return UV_ENOSYS;
}
