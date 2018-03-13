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

int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
	return UV_ENOSYS;
}


int uv_fs_event_start(uv_fs_event_t* handle,
                      uv_fs_event_cb cb,
                      const char* path,
                      unsigned int flags) {
	return UV_ENOSYS;
}


int uv_fs_event_stop(uv_fs_event_t* handle) {
	return UV_ENOSYS;
}


void uv_process_fs_event_req(uv_loop_t* loop, uv_req_t* req,
    uv_fs_event_t* handle) {
  
}


void uv_fs_event_close(uv_loop_t* loop, uv_fs_event_t* handle) {

}


void uv_fs_event_endgame(uv_loop_t* loop, uv_fs_event_t* handle) {

}
