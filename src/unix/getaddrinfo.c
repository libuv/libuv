/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

/* Expose glibc-specific EAI_* error codes. Needs to be defined before we
 * include any headers.
 */

#include "uv.h"
#include "internal.h"
#include "idna.h"

#include <errno.h>
#include <stddef.h> /* NULL */
#include <stdlib.h>
#include <string.h>
#include <net/if.h> /* if_indextoname() */

#if defined(_USE_LIBINFO)
# include <dlfcn.h>
#endif

/* EAI_* constants. */
#include <netdb.h>

static void uv__getaddrinfo_common_done(uv_getaddrinfo_t* req, int status);

#if defined(_USE_LIBINFO)

typedef void getaddrinfo_async_callback(int32_t, struct addrinfo*, void*);
static int32_t (*getaddrinfo_async_start)(mach_port_t*,
                                          const char*,
                                          const char*,
                                          const struct addrinfo*,
                                          getaddrinfo_async_callback,
                                          void*);
static int32_t (*getaddrinfo_async_handle_reply)(void*);
static void (*getaddrinfo_async_cancel)(mach_port_t);

void uv__getaddrinfo_init(void) {
  void* handle = dlopen("libinfo.dylib", RTLD_LAZY | RTLD_LOCAL);

  if (!handle)
    return;

  getaddrinfo_async_start = dlsym(handle, "getaddrinfo_async_start");
  if (!getaddrinfo_async_start)
    goto exit;
  getaddrinfo_async_handle_reply = dlsym(handle, "getaddrinfo_async_handle_reply");
  if (!getaddrinfo_async_handle_reply)
    goto err_reply;
  getaddrinfo_async_cancel = dlsym(handle, "getaddrinfo_async_cancel");
  if (!getaddrinfo_async_cancel)
    goto err_cancel;

  dlclose(handle);
  return;
err_cancel:
  getaddrinfo_async_handle_reply = NULL;
err_reply:
  getaddrinfo_async_start = NULL;
exit:
  dlclose(handle);
}

static void uv__getaddrinfo_async_done(int32_t status,
                                       struct addrinfo* addrinfo,
                                       void* context) {
  uv_hash_t* hash;
  uv_getaddrinfo_t* req;
  uv__io_t* async_addrinfo_io;

  req = context;
  hash = uv__get_loop_hash(req->loop);
  async_addrinfo_io = uv_hash_remove(hash, req);
  assert(async_addrinfo_io != NULL);
  uv_hash_remove(hash, async_addrinfo_io);

  req->addrinfo = addrinfo;
  if (status == UV_UNKNOWN || status == UV_EAI_CANCELED)
    req->retcode = status;
  else
    req->retcode = uv__getaddrinfo_translate_error(status);
  uv__io_close(req->loop, async_addrinfo_io);
  uv__req_unregister(req->loop, req);
  uv__free(async_addrinfo_io);
  uv__getaddrinfo_common_done(req, status);
}

static void uv__getaddrinfo_async_work(uv_loop_t* loop,
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
    uv__getaddrinfo_async_done(UV_UNKNOWN, NULL, req);
  } else {
    getaddrinfo_async_handle_reply(&msg);
  }
}

int uv__getaddrinfo_cancel(uv_getaddrinfo_t* req) {
  uv_hash_t* hash;
  uv__io_t* async_addrinfo_io;

  hash = uv__get_loop_hash(req->loop);
  async_addrinfo_io = uv_hash_find(hash, req);
  if (!async_addrinfo_io) return 1;
  assert(async_addrinfo_io->fd != -1);
  getaddrinfo_async_cancel(async_addrinfo_io->fd);
  uv__getaddrinfo_async_done(UV_EAI_CANCELED, NULL, req);
  return 0;
}
#endif

int uv__getaddrinfo_translate_error(int sys_err) {
  switch (sys_err) {
  case 0: return 0;
#if defined(EAI_ADDRFAMILY)
  case EAI_ADDRFAMILY: return UV_EAI_ADDRFAMILY;
#endif
#if defined(EAI_AGAIN)
  case EAI_AGAIN: return UV_EAI_AGAIN;
#endif
#if defined(EAI_BADFLAGS)
  case EAI_BADFLAGS: return UV_EAI_BADFLAGS;
#endif
#if defined(EAI_BADHINTS)
  case EAI_BADHINTS: return UV_EAI_BADHINTS;
#endif
#if defined(EAI_CANCELED)
  case EAI_CANCELED: return UV_EAI_CANCELED;
#endif
#if defined(EAI_FAIL)
  case EAI_FAIL: return UV_EAI_FAIL;
#endif
#if defined(EAI_FAMILY)
  case EAI_FAMILY: return UV_EAI_FAMILY;
#endif
#if defined(EAI_MEMORY)
  case EAI_MEMORY: return UV_EAI_MEMORY;
#endif
#if defined(EAI_NODATA)
  case EAI_NODATA: return UV_EAI_NODATA;
#endif
#if defined(EAI_NONAME)
# if !defined(EAI_NODATA) || EAI_NODATA != EAI_NONAME
  case EAI_NONAME: return UV_EAI_NONAME;
# endif
#endif
#if defined(EAI_OVERFLOW)
  case EAI_OVERFLOW: return UV_EAI_OVERFLOW;
#endif
#if defined(EAI_PROTOCOL)
  case EAI_PROTOCOL: return UV_EAI_PROTOCOL;
#endif
#if defined(EAI_SERVICE)
  case EAI_SERVICE: return UV_EAI_SERVICE;
#endif
#if defined(EAI_SOCKTYPE)
  case EAI_SOCKTYPE: return UV_EAI_SOCKTYPE;
#endif
#if defined(EAI_SYSTEM)
  case EAI_SYSTEM: return UV__ERR(errno);
#endif
  }
  assert(!"unknown EAI_* error code");
  abort();
#ifndef __SUNPRO_C
  return 0;  /* Pacify compiler. */
#endif
}


static void uv__getaddrinfo_common_done(uv_getaddrinfo_t* req, int status) {
  /* See initialization in uv_getaddrinfo(). */
  if (req->hints)
    uv__free(req->hints);
  else if (req->service)
    uv__free(req->service);
  else if (req->hostname)
    uv__free(req->hostname);
  else
    assert(0);

  req->hints = NULL;
  req->service = NULL;
  req->hostname = NULL;

  if (status == UV_ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  }

  if (req->cb)
    req->cb(req, req->retcode, req->addrinfo);
}


static void uv__getaddrinfo_work(struct uv__work* w) {
  uv_getaddrinfo_t* req;
  int err;

  req = container_of(w, uv_getaddrinfo_t, work_req);
  err = getaddrinfo(req->hostname, req->service, req->hints, &req->addrinfo);
  req->retcode = uv__getaddrinfo_translate_error(err);
}


static void uv__getaddrinfo_done(struct uv__work* w, int status) {
  uv_getaddrinfo_t* req;

  req = container_of(w, uv_getaddrinfo_t, work_req);
  uv__req_unregister(req->loop, req);
  uv__getaddrinfo_common_done(req, status);
}

int uv_getaddrinfo(uv_loop_t* loop,
                   uv_getaddrinfo_t* req,
                   uv_getaddrinfo_cb cb,
                   const char* hostname,
                   const char* service,
                   const struct addrinfo* hints) {
  char hostname_ascii[256];
  size_t hostname_len;
  size_t service_len;
  size_t hints_len;
  size_t len;
  char* buf;
  long rc;

  if (req == NULL || (hostname == NULL && service == NULL))
    return UV_EINVAL;

  /* FIXME(bnoordhuis) IDNA does not seem to work z/OS,
   * probably because it uses EBCDIC rather than ASCII.
   */
#ifdef __MVS__
  (void) &hostname_ascii;
#else
  if (hostname != NULL) {
    rc = uv__idna_toascii(hostname,
                          hostname + strlen(hostname),
                          hostname_ascii,
                          hostname_ascii + sizeof(hostname_ascii));
    if (rc < 0)
      return rc;
    hostname = hostname_ascii;
  }
#endif

  hostname_len = hostname ? strlen(hostname) + 1 : 0;
  service_len = service ? strlen(service) + 1 : 0;
  hints_len = hints ? sizeof(*hints) : 0;
  buf = uv__malloc(hostname_len + service_len + hints_len);

  if (buf == NULL)
    return UV_ENOMEM;

  uv__req_init(loop, req, UV_GETADDRINFO);
  req->loop = loop;
  req->cb = cb;
  req->addrinfo = NULL;
  req->hints = NULL;
  req->service = NULL;
  req->hostname = NULL;
  req->retcode = 0;

  /* order matters, see uv_getaddrinfo_done() */
  len = 0;

  if (hints) {
    req->hints = memcpy(buf + len, hints, sizeof(*hints));
    len += sizeof(*hints);
  }

  if (service) {
    req->service = memcpy(buf + len, service, service_len);
    len += service_len;
  }

  if (hostname)
    req->hostname = memcpy(buf + len, hostname, hostname_len);

  if (cb) {
#if defined(_USE_LIBINFO)
    if (getaddrinfo_async_start &&
        getaddrinfo_async_handle_reply &&
        getaddrinfo_async_cancel) {
      int32_t get_addrinfo_result;
      mach_port_t port;
      uv_hash_t* hash;
      uv__io_t* async_addrinfo_io;
      int r;

      hash = uv__get_loop_hash(req->loop);
      async_addrinfo_io = uv__malloc(sizeof(uv__io_t));
      if (async_addrinfo_io) {
        r = uv_hash_insert(hash, req, async_addrinfo_io);
        if (r) goto err_insert;
        r = uv_hash_insert(hash, async_addrinfo_io, req);
        if (r) goto err_insert_req;
        get_addrinfo_result = getaddrinfo_async_start(&port,
                                                      req->hostname,
                                                      req->service,
                                                      req->hints,
                                                      uv__getaddrinfo_async_done,
                                                      req);
        if (get_addrinfo_result == 0) {
          uv__io_init(async_addrinfo_io, uv__getaddrinfo_async_work, port);
          uv__io_start(req->loop, async_addrinfo_io, UV__POLLMACHPORT);
          return 0;
        }
        uv_hash_remove(hash, async_addrinfo_io);
err_insert_req:
        uv_hash_remove(hash, req);
err_insert:
        uv__free(async_addrinfo_io);
      }
    }
#endif
    uv__work_submit(loop,
                    &req->work_req,
                    UV__WORK_SLOW_IO,
                    uv__getaddrinfo_work,
                    uv__getaddrinfo_done);
    return 0;
  } else {
    uv__getaddrinfo_work(&req->work_req);
    uv__getaddrinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}


void uv_freeaddrinfo(struct addrinfo* ai) {
  if (ai)
    freeaddrinfo(ai);
}


int uv_if_indextoname(unsigned int ifindex, char* buffer, size_t* size) {
  char ifname_buf[UV_IF_NAMESIZE];
  size_t len;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  if (if_indextoname(ifindex, ifname_buf) == NULL)
    return UV__ERR(errno);

  len = strnlen(ifname_buf, sizeof(ifname_buf));

  if (*size <= len) {
    *size = len + 1;
    return UV_ENOBUFS;
  }

  memcpy(buffer, ifname_buf, len);
  buffer[len] = '\0';
  *size = len;

  return 0;
}

int uv_if_indextoiid(unsigned int ifindex, char* buffer, size_t* size) {
  return uv_if_indextoname(ifindex, buffer, size);
}
