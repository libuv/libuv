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

void uv_console_init(void) {
}


int uv_tty_init(uv_loop_t* loop, uv_tty_t* tty, uv_os_fd_t handle, int readable) {
	return UV_ENOSYS;
}

int uv_tty_set_mode(uv_tty_t* tty, uv_tty_mode_t mode) {
	return UV_ENOSYS;
}


int uv_is_tty(uv_os_fd_t file) {
	return UV_ENOSYS;
}


int uv_tty_get_winsize(uv_tty_t* tty, int* width, int* height) {
	return UV_ENOSYS;
}

void uv_process_tty_read_raw_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {
}



void uv_process_tty_read_line_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {
}


void uv_process_tty_read_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {

}


int uv_tty_read_start(uv_tty_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb) {
	return UV_ENOSYS;
}


int uv_tty_read_stop(uv_tty_t* handle) {
	return UV_ENOSYS;
}

int uv_tty_write(uv_loop_t* loop,
                 uv_write_t* req,
                 uv_tty_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 uv_write_cb cb) {
	return UV_ENOSYS;
}


int uv__tty_try_write(uv_tty_t* handle,
                      const uv_buf_t bufs[],
                      unsigned int nbufs) {
	return UV_ENOSYS;
}


void uv_process_tty_write_req(uv_loop_t* loop, uv_tty_t* handle,
  uv_write_t* req) {
	return UV_ENOSYS;
}


void uv_tty_close(uv_tty_t* handle) {

}


void uv_tty_endgame(uv_loop_t* loop, uv_tty_t* handle) {

}


/* TODO: remove me */
void uv_process_tty_accept_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* raw_req) {
}


/* TODO: remove me */
void uv_process_tty_connect_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_connect_t* req) {
}


int uv_tty_reset_mode(void) {
  /* Not necessary to do anything. */
  return 0;
}

static void uv__determine_vterm_state(HANDLE handle) {
	return UV_ENOSYS;
}
