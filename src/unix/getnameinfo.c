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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_USE_LIBINFO)
# include <dlfcn.h>
#endif

#include "uv.h"
#include "internal.h"

static void uv__getnameinfo_common_done(uv_getnameinfo_t* req, int status);

#if defined(_USE_LIBINFO)

typedef void getnameinfo_async_callback(int32_t, char*, char*, void*);
static int32_t (*getnameinfo_async_start)(mach_port_t*,
                                          const struct sockaddr*,
                                          size_t,
                                          int,
                                          getnameinfo_async_callback,
                                          void*);
static int32_t (*getnameinfo_async_handle_reply)(void*);
static void (*getnameinfo_async_cancel)(mach_port_t);

void uv__getnameinfo_init(void) {
  void* handle = dlopen("libinfo.dylib", RTLD_LAZY | RTLD_LOCAL);

  if (!handle)
    return;

  getnameinfo_async_start = dlsym(handle, "getnameinfo_async_start");
  if (!getnameinfo_async_start)
    goto exit;
  getnameinfo_async_handle_reply = dlsym(handle,
                                         "getnameinfo_async_handle_reply");
  if (!getnameinfo_async_handle_reply)
    goto err_reply;
  getnameinfo_async_cancel = dlsym(handle, "getnameinfo_async_cancel");
  if (!getnameinfo_async_cancel)
    goto err_cancel;

  dlclose(handle);
  return;
err_cancel:
  getnameinfo_async_handle_reply = NULL;
err_reply:
  getnameinfo_async_start = NULL;
exit:
  dlclose(handle);
}

static void uv__getnameinfo_async_safe_copy(char* dst,
                                            const char* src,
                                            size_t dst_size) {
  if (src)
    uv__strscpy(dst, src, dst_size);
  else
    memset(dst, 0, dst_size);
}

static void uv__getnameinfo_async_done(int32_t status,
                                       char* host,
                                       char* service,
                                       void* context) {
  uv_getnameinfo_t* req;
  uv_hash_t* hash;
  uv__io_t* async_getnameinfo_io;

  req = context;
  hash = uv__get_loop_hash(req->loop);
  async_getnameinfo_io = uv_hash_remove(hash, req);
  assert(async_getnameinfo_io != NULL);
  uv_hash_remove(hash, async_getnameinfo_io);

  uv__getnameinfo_async_safe_copy(req->host, host, sizeof(req->host));
  uv__getnameinfo_async_safe_copy(req->service, service, sizeof(req->service));
  if (status == UV_UNKNOWN || status == UV_EAI_CANCELED)
    req->retcode = status;
  else
    req->retcode = uv__getaddrinfo_translate_error(status);
  uv__io_close(req->loop, async_getnameinfo_io);
  uv__req_unregister(req->loop, req);
  uv__free(async_getnameinfo_io);
  uv__getnameinfo_common_done(req, status);
}

static void uv__getnameinfo_async_work(uv_loop_t* loop,
                                       uv__io_t* w,
                                       unsigned int fflags) {
  mach_msg_empty_rcv_t msg;
  mach_msg_return_t status;
  uv_hash_t* hash;
  uv_getaddrinfo_t* req;

  status = mach_msg(&msg.header,
                    MACH_RCV_MSG,
                    0,
                    sizeof(msg),
                    w->fd,
                    MACH_MSG_TIMEOUT_NONE,
                    MACH_PORT_NULL);
  if (status != KERN_SUCCESS) {
    hash = uv__get_loop_hash(loop);
    req = uv_hash_find(hash, w);
    assert(req != NULL);
    uv__getnameinfo_async_done(UV_UNKNOWN, NULL, NULL, req);
  } else {
    getnameinfo_async_handle_reply(&msg);
  }
}

int uv__getnameinfo_cancel(uv_getnameinfo_t* req) {
  uv_hash_t* hash;
  uv__io_t* async_getnameinfo_io;

  hash = uv__get_loop_hash(req->loop);
  async_getnameinfo_io = uv_hash_find(hash, req);
  if (!async_getnameinfo_io) return 1;
  assert(async_getnameinfo_io->fd != -1);
  getnameinfo_async_cancel(async_getnameinfo_io->fd);
  uv__getnameinfo_async_done(UV_EAI_CANCELED, NULL, NULL, req);
  return 0;
}
#endif


static void uv__getnameinfo_work(struct uv__work* w) {
  uv_getnameinfo_t* req;
  int err;
  socklen_t salen;

  req = container_of(w, uv_getnameinfo_t, work_req);

  if (req->storage.ss_family == AF_INET)
    salen = sizeof(struct sockaddr_in);
  else if (req->storage.ss_family == AF_INET6)
    salen = sizeof(struct sockaddr_in6);
  else
    abort();

  err = getnameinfo((struct sockaddr*) &req->storage,
                    salen,
                    req->host,
                    sizeof(req->host),
                    req->service,
                    sizeof(req->service),
                    req->flags);
  req->retcode = uv__getaddrinfo_translate_error(err);
}

static void uv__getnameinfo_common_done(uv_getnameinfo_t* req, int status) {
  char* host;
  char* service;
  host = service = NULL;

  if (status == UV_ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  } else if (req->retcode == 0) {
    host = req->host;
    service = req->service;
  }

  if (req->getnameinfo_cb)
    req->getnameinfo_cb(req, req->retcode, host, service);
}

static void uv__getnameinfo_done(struct uv__work* w, int status) {
  uv_getnameinfo_t* req;

  req = container_of(w, uv_getnameinfo_t, work_req);
  uv__req_unregister(req->loop, req);
  uv__getnameinfo_common_done(req, status);
}

/*
* Entry point for getnameinfo
* return 0 if a callback will be made
* return error code if validation fails
*/
int uv_getnameinfo(uv_loop_t* loop,
                   uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const struct sockaddr* addr,
                   int flags) {
  size_t salen;

  if (req == NULL || addr == NULL)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET) {
    salen = sizeof(struct sockaddr_in);
    memcpy(&req->storage,
           addr,
           salen);
  } else if (addr->sa_family == AF_INET6) {
    salen = sizeof(struct sockaddr_in6);
    memcpy(&req->storage,
           addr,
           salen);
  } else {
    return UV_EINVAL;
  }

  uv__req_init(loop, (uv_req_t*)req, UV_GETNAMEINFO);

  req->getnameinfo_cb = getnameinfo_cb;
  req->flags = flags;
  req->type = UV_GETNAMEINFO;
  req->loop = loop;
  req->retcode = 0;

  if (getnameinfo_cb) {
#if defined(_USE_LIBINFO)
    if (getnameinfo_async_start &&
        getnameinfo_async_handle_reply &&
        getnameinfo_async_cancel) {
      int32_t get_nameinfo_result;
      mach_port_t port;
      uv_hash_t* hash;
      uv__io_t* async_getnameinfo_io;
      int r;

      hash = uv__get_loop_hash(req->loop);
      async_getnameinfo_io = uv__malloc(sizeof(uv__io_t));
      if (async_getnameinfo_io) {
        r = uv_hash_insert(hash, req, async_getnameinfo_io);
        if (r) goto err_insert;
        r = uv_hash_insert(hash, async_getnameinfo_io, req);
        if (r) goto err_insert_req;
        get_nameinfo_result =
          getnameinfo_async_start(&port,
                                  (struct sockaddr*) &req->storage,
                                  salen,
                                  req->flags,
                                  uv__getnameinfo_async_done,
                                  req);
        if (get_nameinfo_result == 0) {
          uv__io_init(async_getnameinfo_io,
                      uv__getnameinfo_async_work,
                      port);
          uv__io_start(req->loop, async_getnameinfo_io, UV__POLLMACHPORT);
          return 0;
        }
        uv_hash_remove(hash, async_getnameinfo_io);
err_insert_req:
        uv_hash_remove(hash, req);
err_insert:
        uv__free(async_getnameinfo_io);
      }
    }
#endif
    uv__work_submit(loop,
                    &req->work_req,
                    UV__WORK_SLOW_IO,
                    uv__getnameinfo_work,
                    uv__getnameinfo_done);
    return 0;
  } else {
    uv__getnameinfo_work(&req->work_req);
    uv__getnameinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}
