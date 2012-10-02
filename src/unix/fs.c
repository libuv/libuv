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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>

#if defined(__linux__) || defined(__sun)
# include <sys/sendfile.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
# include <sys/socket.h>
# include <sys/uio.h>
#endif

#define INIT(type)                                                            \
  do {                                                                        \
    uv__req_init((loop), (req), UV_FS_ ## type);                              \
    (req)->fs_type = UV_FS_ ## type;                                          \
    (req)->errorno = 0;                                                       \
    (req)->result = 0;                                                        \
    (req)->ptr = NULL;                                                        \
    (req)->loop = loop;                                                       \
    (req)->path = NULL;                                                       \
    (req)->new_path = NULL;                                                   \
    (req)->cb = (cb);                                                         \
  }                                                                           \
  while (0)

#define PATH                                                                  \
  do {                                                                        \
    if (NULL == ((req)->path = strdup((path))))                               \
      return uv__set_sys_error((loop), ENOMEM);                               \
  }                                                                           \
  while (0)

#define PATH2                                                                 \
  do {                                                                        \
    size_t path_len;                                                          \
    size_t new_path_len;                                                      \
                                                                              \
    path_len = strlen((path)) + 1;                                            \
    new_path_len = strlen((new_path)) + 1;                                    \
                                                                              \
    if (NULL == ((req)->path = malloc(path_len + new_path_len)))              \
      return uv__set_sys_error((loop), ENOMEM);                               \
                                                                              \
    (req)->new_path = (req)->path + path_len;                                 \
    memcpy((void*) (req)->path, (path), path_len);                            \
    memcpy((void*) (req)->new_path, (new_path), new_path_len);                \
  }                                                                           \
  while (0)

#define POST                                                                  \
  do {                                                                        \
    if ((cb) != NULL)                                                         \
      uv__work_submit((loop), &(req)->work_req, uv__fs_work, uv__fs_done);    \
    else {                                                                    \
      uv__fs_work(&(req)->work_req);                                          \
      uv__fs_done(&(req)->work_req);                                          \
    }                                                                         \
    return (req)->result;                                                     \
  }                                                                           \
  while (0)


static ssize_t uv__fs_fdatasync(uv_fs_t* req) {
#if defined(__APPLE__) && defined(F_FULLFSYNC)
  return fcntl(req->file, F_FULLFSYNC);
#else
  return fdatasync(req->file);
#endif
}


static ssize_t uv__fs_futime(uv_fs_t* req) {
#if defined(__linux__)
  /* utimesat() has nanosecond resolution but we stick to microseconds
   * for the sake of consistency with other platforms.
   */
  struct timespec ts[2];
  ts[0].tv_sec  = req->atime;
  ts[0].tv_nsec = (unsigned long)(req->atime * 1000000) % 1000000 * 1000;
  ts[1].tv_sec  = req->mtime;
  ts[1].tv_nsec = (unsigned long)(req->mtime * 1000000) % 1000000 * 1000;
  return uv__utimesat(req->file, NULL, ts, 0);
#elif HAVE_FUTIMES
  struct timeval tv[2];
  tv[0].tv_sec  = req->atime;
  tv[0].tv_usec = (unsigned long)(req->atime * 1000000) % 1000000;
  tv[1].tv_sec  = req->mtime;
  tv[1].tv_usec = (unsigned long)(req->mtime * 1000000) % 1000000;
  return futimes(req->file, tv);
#else /* !HAVE_FUTIMES */
  errno = ENOSYS;
  return -1;
#endif
}


static ssize_t uv__fs_pwrite(uv_fs_t* req) {
#if defined(__APPLE__)
  /* Serialize writes on OS X, concurrent pwrite() calls result in data loss.
   * We can't use a per-file descriptor lock, the descriptor may be a dup().
   */
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  ssize_t r;

  pthread_mutex_lock(&lock);
  r = pwrite(req->file, req->buf, req->len, req->off);
  pthread_mutex_unlock(&lock);

  return r;
#else
  return pwrite(req->file, req->buf, req->len, req->off);
#endif
}

static ssize_t uv__fs_read(uv_fs_t* req) {
  if (req->off < 0)
    return read(req->file, req->buf, req->len);
  else
    return pread(req->file, req->buf, req->len, req->off);
}


#if defined(__linux__) || defined(__sun)
static int uv__fs_readdir_filter(const struct dirent* dent) {
#else
static int uv__fs_readdir_filter(struct dirent* dent) {
#endif
  return strcmp(dent->d_name, ".") != 0 && strcmp(dent->d_name, "..") != 0;
}


/* This should have been called uv__fs_scandir(). */
static ssize_t uv__fs_readdir(uv_fs_t* req) {
  struct dirent **dents;
  int saved_errno;
  size_t off;
  size_t len;
  char *buf;
  int i;
  int n;

  n = scandir(req->path, &dents, uv__fs_readdir_filter, alphasort);

  if (n == -1 || n == 0)
    return n;

  len = 0;

  for (i = 0; i < n; i++)
    len += strlen(dents[i]->d_name) + 1;

  buf = malloc(len);

  if (buf == NULL) {
    errno = ENOMEM;
    n = -1;
    goto out;
  }

  off = 0;

  for (i = 0; i < n; i++) {
    len = strlen(dents[i]->d_name) + 1;
    memcpy(buf + off, dents[i]->d_name, len);
    off += len;
  }

  req->ptr = buf;

out:
  saved_errno = errno;
  {
    for (i = 0; i < n; i++)
      free(dents[i]);
    free(dents);
  }
  errno = saved_errno;

  return n;
}


static ssize_t uv__fs_readlink(uv_fs_t* req) {
  ssize_t len;
  char* buf;

  len = pathconf(req->path, _PC_PATH_MAX);

  if (len == -1) {
#if defined(PATH_MAX)
    len = PATH_MAX;
#else
    len = 4096;
#endif
  }

  buf = malloc(len + 1);

  if (buf == NULL) {
    errno = ENOMEM;
    return -1;
  }

  len = readlink(req->path, buf, len);

  if (len == -1) {
    free(buf);
    return -1;
  }

  buf[len] = '\0';
  req->ptr = buf;

  return 0;
}


static ssize_t uv__fs_sendfile(uv_fs_t* req) {
  /* req->file is the out_fd, req->flags the in_fd */
#if defined(__linux__) || defined(__sun)
  return sendfile(req->file, req->flags, &req->off, req->len);
#elif defined(__APPLE__) || defined(__FreeBSD__)
  return sendfile(req->flags,
                  req->file,
                  req->off,
                  req->len,
                  NULL,
                  &req->off,
                  0);
#else
  errno = ENOSYS;
  return -1;
#endif
}


static ssize_t uv__fs_utime(uv_fs_t* req) {
  struct utimbuf buf;
  buf.actime = req->atime;
  buf.modtime = req->mtime;
  return utime(req->path, &buf); /* TODO use utimes() where available */
}


static ssize_t uv__fs_write(uv_fs_t* req) {
  if (req->off < 0)
    return write(req->file, req->buf, req->len);
  else
    return uv__fs_pwrite(req);
}


static void uv__fs_work(struct uv__work* w) {
  int retry_on_eintr;
  uv_fs_t* req;
  ssize_t r;

  req = container_of(w, uv_fs_t, work_req);
  retry_on_eintr = !(req->fs_type == UV_FS_CLOSE);

  do {
    errno = 0;

#define X(type, action)                                                       \
  case UV_FS_ ## type:                                                        \
    r = action;                                                               \
    break;

    switch (req->fs_type) {
    X(CHMOD, chmod(req->path, req->mode));
    X(CHOWN, chown(req->path, req->uid, req->gid));
    X(CLOSE, close(req->file));
    X(FCHMOD, fchmod(req->file, req->mode));
    X(FCHOWN, fchown(req->file, req->uid, req->gid));
    X(FDATASYNC, uv__fs_fdatasync(req));
    X(FSTAT, fstat(req->file, &req->statbuf));
    X(FSYNC, fsync(req->file));
    X(FTRUNCATE, ftruncate(req->file, req->off));
    X(FUTIME, uv__fs_futime(req));
    X(LSTAT, lstat(req->path, &req->statbuf));
    X(LINK, link(req->path, req->new_path));
    X(MKDIR, mkdir(req->path, req->mode));
    X(OPEN, open(req->path, req->flags, req->mode));
    X(READ, uv__fs_read(req));
    X(READDIR, uv__fs_readdir(req));
    X(READLINK, uv__fs_readlink(req));
    X(RENAME, rename(req->path, req->new_path));
    X(RMDIR, rmdir(req->path));
    X(SENDFILE, uv__fs_sendfile(req));
    X(STAT, stat(req->path, &req->statbuf));
    X(SYMLINK, symlink(req->path, req->new_path));
    X(UNLINK, unlink(req->path));
    X(UTIME, uv__fs_utime(req));
    X(WRITE, uv__fs_write(req));
    default: abort();
    }

#undef X
  }
  while (r == -1 && errno == EINTR && retry_on_eintr);

  req->errorno = errno;
  req->result = r;

  if (r == 0 && (req->fs_type == UV_FS_STAT ||
                 req->fs_type == UV_FS_FSTAT ||
                 req->fs_type == UV_FS_LSTAT)) {
    req->ptr = &req->statbuf;
  }
}


static void uv__fs_done(struct uv__work* w) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->errorno != 0) {
    req->errorno = uv_translate_sys_error(req->errorno);
    uv__set_artificial_error(req->loop, req->errorno);
  }

  if (req->cb != NULL)
    req->cb(req);
}


int uv_fs_chmod(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int mode,
                uv_fs_cb cb) {
  INIT(CHMOD);
  PATH;
  req->mode = mode;
  POST;
}


int uv_fs_chown(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int uid,
                int gid,
                uv_fs_cb cb) {
  INIT(CHOWN);
  PATH;
  req->uid = uid;
  req->gid = gid;
  POST;
}


int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(CLOSE);
  req->file = file;
  POST;
}


int uv_fs_fchmod(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 int mode,
                 uv_fs_cb cb) {
  INIT(FCHMOD);
  req->file = file;
  req->mode = mode;
  POST;
}


int uv_fs_fchown(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 int uid,
                 int gid,
                 uv_fs_cb cb) {
  INIT(FCHOWN);
  req->file = file;
  req->uid = uid;
  req->gid = gid;
  POST;
}


int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FDATASYNC);
  req->file = file;
  POST;
}


int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FSTAT);
  req->file = file;
  POST;
}


int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FSYNC);
  req->file = file;
  POST;
}


int uv_fs_ftruncate(uv_loop_t* loop,
                    uv_fs_t* req,
                    uv_file file,
                    int64_t off,
                    uv_fs_cb cb) {
  INIT(FTRUNCATE);
  req->file = file;
  req->off = off;
  POST;
}


int uv_fs_futime(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 double atime,
                 double mtime,
                 uv_fs_cb cb) {
  INIT(FUTIME);
  req->file = file;
  req->atime = atime;
  req->mtime = mtime;
  POST;
}


int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(LSTAT);
  PATH;
  POST;
}


int uv_fs_link(uv_loop_t* loop,
               uv_fs_t* req,
               const char* path,
               const char* new_path,
               uv_fs_cb cb) {
  INIT(LINK);
  PATH2;
  POST;
}


int uv_fs_mkdir(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int mode,
                uv_fs_cb cb) {
  INIT(MKDIR);
  PATH;
  req->mode = mode;
  POST;
}


int uv_fs_open(uv_loop_t* loop,
               uv_fs_t* req,
               const char* path,
               int flags,
               int mode,
               uv_fs_cb cb) {
  INIT(OPEN);
  PATH;
  req->flags = flags;
  req->mode = mode;
  POST;
}


int uv_fs_read(uv_loop_t* loop, uv_fs_t* req,
               uv_file file,
               void* buf,
               size_t len,
               int64_t off,
               uv_fs_cb cb) {
  INIT(READ);
  req->file = file;
  req->buf = buf;
  req->len = len;
  req->off = off;
  POST;
}


int uv_fs_readdir(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* path,
                  int flags,
                  uv_fs_cb cb) {
  INIT(READDIR);
  PATH;
  req->flags = flags;
  POST;
}


int uv_fs_readlink(uv_loop_t* loop,
                   uv_fs_t* req,
                   const char* path,
                   uv_fs_cb cb) {
  INIT(READLINK);
  PATH;
  POST;
}


int uv_fs_rename(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 const char* new_path,
                 uv_fs_cb cb) {
  INIT(RENAME);
  PATH2;
  POST;
}


int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(RMDIR);
  PATH;
  POST;
}


int uv_fs_sendfile(uv_loop_t* loop,
                   uv_fs_t* req,
                   uv_file out_fd,
                   uv_file in_fd,
                   int64_t off,
                   size_t len,
                   uv_fs_cb cb) {
  INIT(SENDFILE);
  req->flags = in_fd; /* hack */
  req->file = out_fd;
  req->off = off;
  req->len = len;
  POST;
}


int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(STAT);
  PATH;
  POST;
}


int uv_fs_symlink(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* path,
                  const char* new_path,
                  int flags,
                  uv_fs_cb cb) {
  INIT(SYMLINK);
  PATH2;
  req->flags = flags;
  POST;
}


int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(UNLINK);
  PATH;
  POST;
}


int uv_fs_utime(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                double atime,
                double mtime,
                uv_fs_cb cb) {
  INIT(UTIME);
  PATH;
  req->atime = atime;
  req->mtime = mtime;
  POST;
}


int uv_fs_write(uv_loop_t* loop,
                uv_fs_t* req,
                uv_file file,
                void* buf,
                size_t len,
                int64_t off,
                uv_fs_cb cb) {
  INIT(WRITE);
  req->file = file;
  req->buf = buf;
  req->len = len;
  req->off = off;
  POST;
}


void uv_fs_req_cleanup(uv_fs_t* req) {
  free((void*) req->path);
  req->path = NULL;
  req->new_path = NULL;

  if (req->ptr != &req->statbuf)
    free(req->ptr);
  req->ptr = NULL;
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  if (req->work_cb)
    req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  uv__req_unregister(req->loop, req);

  if (req->after_work_cb)
    req->after_work_cb(req);
}


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
}
