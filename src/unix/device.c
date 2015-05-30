/* Copyright The libuv project and contributors. All rights reserved.
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
#include "internal.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int uv__device_open(uv_loop_t* loop,
                           uv_device_t* device,
                           uv_os_fd_t fd,
                           int flags) {
  int err;
  int uvflags = 0;
  struct stat s;
  assert(device);

  if (flags == O_RDONLY)
    uvflags |= UV_STREAM_READABLE;
  else if (flags == O_WRONLY)
    uvflags |= UV_STREAM_WRITABLE;
  else if (flags == O_RDWR)
    uvflags |= UV_STREAM_READABLE | UV_STREAM_WRITABLE;

  if (fstat(fd, &s))
    return UV_UNKNOWN_HANDLE;
  if (!S_ISCHR(s.st_mode) && !S_ISBLK(s.st_mode))
    return UV_UNKNOWN_HANDLE; 

  uv__stream_init(loop, (uv_stream_t*) device, UV_DEVICE);
  err = uv__nonblock(fd, 1);
  if (err)
    return err;

  err = uv__stream_open((uv_stream_t*) device, fd, uvflags);
  if (err)
    return err;
  return 0;
}

int uv_device_open(uv_loop_t* loop,
                   uv_device_t* device,
                   uv_os_fd_t fd) {
  return uv__device_open(loop, device, fd, O_RDWR);
}

int uv_device_init(uv_loop_t* loop,
                   uv_device_t* device,
                   const char* path,
                   int flags) {
  int fd, err;

  assert(device);
  if ((flags & O_ACCMODE) != O_RDONLY && 
      (flags & O_ACCMODE) != O_WRONLY && 
      (flags & O_ACCMODE) != O_RDWR)
    return -EINVAL;

  fd = open(path, flags); 
  if (fd < 0) 
    return -errno;

  err = uv__device_open(loop, device, fd, flags & O_ACCMODE);
  if (err != 0) {
    close(fd);
    return err;
  }
  return 0;
}

int uv_device_ioctl(uv_device_t* device,
                    unsigned int cmd,
                    uv_ioargs_t* args) {
  int err = ioctl(uv__stream_fd((uv_stream_t*) device), cmd, args->arg);
  if (err < 0)
    return -errno;
  return err;
}

void uv__device_close(uv_device_t* handle) {
  uv__stream_close((uv_stream_t*) handle);
}
