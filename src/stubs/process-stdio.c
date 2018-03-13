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

void uv_disable_stdio_inheritance(void) {
}


int uv__create_nul_handle(HANDLE* handle_ptr,
    DWORD access) {
	return UV_ENOSYS;
}


int uv__stdio_create(uv_loop_t* loop,
                     const uv_process_options_t* options,
                     BYTE** buffer_ptr) {
	return UV_ENOSYS;
}


void uv__stdio_destroy(BYTE* buffer) {
}


void uv__stdio_noinherit(BYTE* buffer) {
}


int uv__stdio_verify(BYTE* buffer, WORD size) {
	return UV_ENOSYS;
}


WORD uv__stdio_size(BYTE* buffer) {
	return 0;
}


HANDLE uv__stdio_handle(BYTE* buffer, int fd) {
	return NULL;
}
