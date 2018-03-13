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

void fs__open(uv_fs_t* req) {
  
}

void fs__close(uv_fs_t* req) {

}


void fs__read(uv_fs_t* req) {
  
}


void fs__write(uv_fs_t* req) {
  
}


void fs__rmdir(uv_fs_t* req) {

}


void fs__unlink(uv_fs_t* req) {
  
}


void fs__mkdir(uv_fs_t* req) {

}


void fs__mkdtemp(uv_fs_t* req) {

}


void fs__scandir(uv_fs_t* req) {

}


void uv_fs_req_cleanup(uv_fs_t* req) {

}

void uv_fs_init(void) {

}


int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
    int mode, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_read(uv_loop_t* loop,
               uv_fs_t* req,
               uv_os_fd_t handle,
               const uv_buf_t bufs[],
               unsigned int nbufs,
               int64_t offset,
               uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_write(uv_loop_t* loop,
                uv_fs_t* req,
                uv_os_fd_t handle,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                int64_t offset,
                uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_mkdir(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_mkdtemp(uv_loop_t* loop, uv_fs_t* req, const char* tpl,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_scandir(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_link(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_symlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, int flags, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_readlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_realpath(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_chown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid,
    uv_gid_t gid, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_fchown(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t hFile, uv_uid_t uid,
    uv_gid_t gid, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_rename(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle,
    int64_t offset, uv_fs_cb cb) {
	return UV_ENOSYS;
}



int uv_fs_sendfile(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t fd_out,
    uv_os_fd_t fd_in, int64_t in_offset, size_t length, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_access(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 int flags,
                 uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_chmod(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_fchmod(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, int mode,
    uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_utime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime,
    double mtime, uv_fs_cb cb) {
	return UV_ENOSYS;
}


int uv_fs_futime(uv_loop_t* loop, uv_fs_t* req, uv_os_fd_t handle, double atime,
    double mtime, uv_fs_cb cb) {
	return UV_ENOSYS;
}
