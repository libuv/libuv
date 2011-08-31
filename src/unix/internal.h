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

#ifndef UV_UNIX_INTERNAL_H_
#define UV_UNIX_INTERNAL_H_

#include "uv-common.h"
#include "uv-eio.h"

int uv__close(int fd);
void uv__req_init(uv_req_t*);
uv_err_t uv_err_new(uv_loop_t* loop, int sys_error);
uv_err_t uv_err_new_artificial(uv_loop_t* loop, int code);
int uv__nonblock(int fd, int set) __attribute__((unused));
int uv__cloexec(int fd, int set) __attribute__((unused));
int uv__socket(int domain, int type, int protocol);
void uv__handle_init(uv_loop_t* loop, uv_handle_t* handle, uv_handle_type type);

/* udp */
void uv__udp_destroy(uv_udp_t* handle);
void uv__udp_watcher_stop(uv_udp_t* handle, ev_io* w);

#endif /* UV_UNIX_INTERNAL_H_ */
