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
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_SOCK_NAME_LEN  (int)(sizeof(struct sockaddr_un) - offsetof(struct sockaddr_un, sun_path))


int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc) {
  uv__stream_init(loop, (uv_stream_t*)handle, UV_NAMED_PIPE);
  handle->shutdown_req = NULL;
  handle->connect_req = NULL;
  handle->pipe_fname = NULL;
  handle->ipc = ipc;
  return 0;
}

/*
 * If buf is NULL it will return a pointer to allocated memory that
 * must be released with free() when it is no more used.
 */
char * uv_build_abstract_socket_name(char *name, int length, char *buf) {
  char *dest;

  if ((length + 1 > MAX_SOCK_NAME_LEN) || (length <= 0)) return NULL;

  if (buf == NULL) {
    buf = malloc(MAX_SOCK_NAME_LEN);
    if (buf == NULL)
      return NULL;
  }

  dest = buf;

  *dest++ = '\0';
  *dest++ = length;
  memcpy(dest, name, length);
  return buf;
}


int uv__get_pipe_name(const char *name, char *pipe_name, const char **pallocated, int max_len) {
  int len;
  if (name[0]==0) {  /* abstract socket */
    len = name[1];
    if (len > max_len) return -ENAMETOOLONG;
    if (pallocated) {
      pipe_name = uv__malloc(len+1);
      if (pipe_name == NULL) return UV_ENOMEM;
    }
    pipe_name[0] = '\0';  /* store the starting null char */
    memcpy(&pipe_name[1], &name[2], len);
    len++;                /* include the first null char */
  } else {
    len = (int)strlen(name);
    if (len > max_len) return -ENAMETOOLONG;
    if (pallocated) {
      pipe_name = uv__strdup(name);
      if (pipe_name == NULL) return UV_ENOMEM;
    } else {
      strcpy(pipe_name, name);
    }
  }
  if (pallocated)
    *pallocated = pipe_name;
  return len;
}


int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
  struct sockaddr_un saddr;
  const char* pipe_fname;
  int sockfd;
  int err;
  int addr_len;
  int name_len;

  pipe_fname = NULL;
  sockfd = -1;

  /* Already bound? */
  if (uv__stream_fd(handle) >= 0)
    return UV_EINVAL;

  /* Process the pipe name and get a copy of it that outlives this function's scope. */
  name_len = uv__get_pipe_name(name, NULL, &pipe_fname, sizeof(saddr.sun_path) - 1);
  if (name_len <= 0)
    return name_len;

  /* We've got a copy, don't touch the original any more. */
  name = NULL;

  err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
  if (err < 0)
    goto err_socket;
  sockfd = err;

  memset(&saddr, 0, sizeof saddr);
  memcpy(saddr.sun_path, pipe_fname, name_len);
  saddr.sun_family = AF_UNIX;

  if (pipe_fname[0] == 0)
    addr_len = offsetof(struct sockaddr_un, sun_path) + name_len;
  else
    addr_len = sizeof saddr;

  if (bind(sockfd, (struct sockaddr*)&saddr, addr_len)) {
    err = UV__ERR(errno);
    /* Convert ENOENT to EACCES for compatibility with Windows. */
    if (err == UV_ENOENT)
      err = UV_EACCES;

    uv__close(sockfd);
    goto err_socket;
  }

  /* Success. */
  handle->flags |= UV_HANDLE_BOUND;
  handle->pipe_fname = pipe_fname; /* Is a strdup'ed copy. */
  handle->io_watcher.fd = sockfd;
  return 0;

err_socket:
  uv__free((void*)pipe_fname);
  return err;
}


int uv_pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb) {
  if (uv__stream_fd(handle) == -1)
    return UV_EINVAL;

#if defined(__MVS__)
  /* On zOS, backlog=0 has undefined behaviour */
  if (backlog == 0)
    backlog = 1;
  else if (backlog < 0)
    backlog = SOMAXCONN;
#endif

  if (listen(uv__stream_fd(handle), backlog))
    return UV__ERR(errno);

  handle->connection_cb = cb;
  handle->io_watcher.cb = uv__server_io;
  uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
  return 0;
}


void uv__pipe_close(uv_pipe_t* handle) {
  if (handle->pipe_fname) {
    /*
     * Unlink the file system entity before closing the file descriptor.
     * Doing it the other way around introduces a race where our process
     * unlinks a socket with the same name that's just been created by
     * another thread or process.
     * If it is an abstract socket unlink will see it as an empty path.
     */
    unlink(handle->pipe_fname);
    uv__free((void*)handle->pipe_fname);
    handle->pipe_fname = NULL;
  }

  uv__stream_close((uv_stream_t*)handle);
}


int uv_pipe_open(uv_pipe_t* handle, uv_os_fd_t fd) {
  int err;

  err = uv__nonblock(fd, 1);
  if (err)
    return err;

#if defined(__APPLE__)
  err = uv__stream_try_select((uv_stream_t*) handle, &fd);
  if (err)
    return err;
#endif /* defined(__APPLE__) */

  return uv__stream_open((uv_stream_t*)handle,
                         fd,
                         UV_STREAM_READABLE | UV_STREAM_WRITABLE);
}


void uv_pipe_connect(uv_connect_t* req,
                    uv_pipe_t* handle,
                    const char* name,
                    uv_connect_cb cb) {
  struct sockaddr_un saddr;
  int new_sock;
  int err;
  int r;
  int addr_len;
  int name_len;
  char pipe_name[128];

  /* Process the pipe name */
  name_len = uv__get_pipe_name(name, pipe_name, NULL, sizeof(saddr.sun_path) - 1);
  if (name_len <= 0) {
    err = name_len;
    goto out;
  }

  new_sock = (uv__stream_fd(handle) == -1);

  if (new_sock) {
    err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
    if (err < 0)
      goto out;
    handle->io_watcher.fd = err;
  }

  memset(&saddr, 0, sizeof saddr);
  memcpy(saddr.sun_path, pipe_name, name_len);
  saddr.sun_family = AF_UNIX;

  if (pipe_name[0] == 0)  /* abstract socket */
    addr_len = offsetof(struct sockaddr_un, sun_path) + name_len;
  else
    addr_len = sizeof saddr;

  do {
    r = connect(uv__stream_fd(handle),
                (struct sockaddr*)&saddr, addr_len);
  }
  while (r == -1 && errno == EINTR);

  if (r == -1 && errno != EINPROGRESS) {
    err = UV__ERR(errno);
#if defined(__CYGWIN__) || defined(__MSYS__)
    /* EBADF is supposed to mean that the socket fd is bad, but
       Cygwin reports EBADF instead of ENOTSOCK when the file is
       not a socket.  We do not expect to see a bad fd here
       (e.g. due to new_sock), so translate the error.  */
    if (err == UV_EBADF)
      err = UV_ENOTSOCK;
#endif
    goto out;
  }

  err = 0;
  if (new_sock) {
    err = uv__stream_open((uv_stream_t*)handle,
                          uv__stream_fd(handle),
                          UV_STREAM_READABLE | UV_STREAM_WRITABLE);
  }

  if (err == 0)
    uv__io_start(handle->loop, &handle->io_watcher, POLLIN | POLLOUT);

out:
  handle->delayed_error = err;
  handle->connect_req = req;

  uv__req_init(handle->loop, req, UV_CONNECT);
  req->handle = (uv_stream_t*)handle;
  req->cb = cb;
  QUEUE_INIT(&req->queue);

  /* Force callback to run on next tick in case of error. */
  if (err)
    uv__io_feed(handle->loop, &handle->io_watcher);

}


typedef int (*uv__peersockfunc)(int, struct sockaddr*, socklen_t*);


static int uv__pipe_getsockpeername(const uv_pipe_t* handle,
                                    uv__peersockfunc func,
                                    char* buffer,
                                    size_t* size) {
  struct sockaddr_un sa;
  socklen_t addrlen;
  int err;

  addrlen = sizeof(sa);
  memset(&sa, 0, addrlen);
  err = func(uv__stream_fd(handle), (struct sockaddr*) &sa, &addrlen);
  if (err < 0) {
    *size = 0;
    return UV__ERR(errno);
  }

#if defined(__linux__)
  if (sa.sun_path[0] == 0)
    /* Linux abstract namespace */
    addrlen -= offsetof(struct sockaddr_un, sun_path);
  else
#endif
    addrlen = strlen(sa.sun_path);


  if (addrlen >= *size) {
    *size = addrlen + 1;
    return UV_ENOBUFS;
  }

  memcpy(buffer, sa.sun_path, addrlen);
  *size = addrlen;

  /* only null-terminate if it's not an abstract socket */
  if (buffer[0] != '\0')
    buffer[addrlen] = '\0';

  return 0;
}


int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return uv__pipe_getsockpeername(handle, getsockname, buffer, size);
}


int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return uv__pipe_getsockpeername(handle, getpeername, buffer, size);
}


void uv_pipe_pending_instances(uv_pipe_t* handle, int count) {
}


int uv_pipe_pending_count(uv_pipe_t* handle) {
  uv__stream_queued_fds_t* queued_fds;

  if (!handle->ipc)
    return 0;

  if (handle->accepted_fd == -1)
    return 0;

  if (handle->queued_fds == NULL)
    return 1;

  queued_fds = handle->queued_fds;
  return queued_fds->offset + 1;
}


uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle) {
  if (!handle->ipc)
    return UV_UNKNOWN_HANDLE;

  if (handle->accepted_fd == -1)
    return UV_UNKNOWN_HANDLE;
  else
    return uv__handle_type(handle->accepted_fd);
}


int uv_pipe_chmod(uv_pipe_t* handle, int mode) {
  unsigned desired_mode;
  struct stat pipe_stat;
  char* name_buffer;
  size_t name_len;
  int r;

  if (handle == NULL || uv__stream_fd(handle) == -1)
    return UV_EBADF;

  if (mode != UV_READABLE &&
      mode != UV_WRITABLE &&
      mode != (UV_WRITABLE | UV_READABLE))
    return UV_EINVAL;

  if (fstat(uv__stream_fd(handle), &pipe_stat) == -1)
    return UV__ERR(errno);

  desired_mode = 0;
  if (mode & UV_READABLE)
    desired_mode |= S_IRUSR | S_IRGRP | S_IROTH;
  if (mode & UV_WRITABLE)
    desired_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

  /* Exit early if pipe already has desired mode. */
  if ((pipe_stat.st_mode & desired_mode) == desired_mode)
    return 0;

  pipe_stat.st_mode |= desired_mode;

  /* Unfortunately fchmod does not work on all platforms, we will use chmod. */
  name_len = 0;
  r = uv_pipe_getsockname(handle, NULL, &name_len);
  if (r != UV_ENOBUFS)
    return r;

  name_buffer = uv__malloc(name_len);
  if (name_buffer == NULL)
    return UV_ENOMEM;

  r = uv_pipe_getsockname(handle, name_buffer, &name_len);
  if (r != 0) {
    uv__free(name_buffer);
    return r;
  }

  r = chmod(name_buffer, pipe_stat.st_mode);
  uv__free(name_buffer);

  return r != -1 ? 0 : UV__ERR(errno);
}
