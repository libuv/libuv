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
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h> 
#include <assert.h>

#ifdef WIN32
#define NETWORK_ADAPTER_GUID "{4D36E972-E325-11CE-BFC1-08002BE10318}"

#define ADAPTER_KEY \
    "SYSTEM\\CurrentControlSet\\Control\\Class\\" NETWORK_ADAPTER_GUID

#define NETWORK_CONNECTIONS_KEY \
    "SYSTEM\\CurrentControlSet\\Control\\Network\\" NETWORK_ADAPTER_GUID

#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define TAPSUFFIX         ".tap"

#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1,  METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2,  METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3,  METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4,  METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5,  METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6,  METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7,  METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8,  METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9,  METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_TUN            TAP_CONTROL_CODE (10, METHOD_BUFFERED)

static int is_tap_win32_dev(const char *guid) {
  HKEY netcard_key;
  DWORD len;
  int i = 0;

  LONG status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                        ADAPTER_KEY,
                        0,
                        KEY_READ,
                        &netcard_key);

  if (status != ERROR_SUCCESS) 
    return FALSE;

  for (;;) {
    char enum_name[256];
    char unit_string[256];
    HKEY unit_key;
    char component_id_string[] = "ComponentId";
    char component_id[256];
    char net_cfg_instance_id_string[] = "NetCfgInstanceId";
    char net_cfg_instance_id[256];
    DWORD data_type;

    len = sizeof (enum_name);
    status = RegEnumKeyEx(netcard_key,
                          i,
                          enum_name,
                          &len,
                          NULL,
                          NULL,
                          NULL,
                          NULL);

    if (status == ERROR_NO_MORE_ITEMS) 
      break;
    else if (status != ERROR_SUCCESS) 
      return FALSE;

    _snprintf (unit_string, 
               sizeof(unit_string), 
               "%s\\%s",
                ADAPTER_KEY,
                enum_name);

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                          unit_string,
                          0,
                          KEY_READ,
                          &unit_key);

    if (status != ERROR_SUCCESS) 
      return FALSE;
    else {
      len = sizeof (component_id);
      status = RegQueryValueEx(unit_key,
                               component_id_string,
                               NULL,
                               &data_type,
                               (uint8_t*) component_id,
                               &len);

      if (!(status != ERROR_SUCCESS || data_type != REG_SZ)) {
        len = sizeof (net_cfg_instance_id);
        status = RegQueryValueEx(unit_key,
                                 net_cfg_instance_id_string,
                                 NULL,
                                 &data_type,
                                 (uint8_t*) net_cfg_instance_id,
                                 &len);

        if (status == ERROR_SUCCESS && data_type == REG_SZ) {
          if (!memcmp(component_id, "tap", strlen("tap")) &&
              !strcmp (net_cfg_instance_id, guid)) {
              RegCloseKey (unit_key);
              RegCloseKey (netcard_key);
              return TRUE;
          }
        }
      }
      RegCloseKey (unit_key);
    }
    ++i;
  }

  RegCloseKey (netcard_key);
  return FALSE;
}

static int get_device_guid(char *name,
                           int name_size,
                           char *actual_name,
                           int actual_name_size) {
  HKEY control_net_key;
  DWORD len;
  int stop = 0;
  int i;

  LONG status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                        NETWORK_CONNECTIONS_KEY, 
                        0, 
                        KEY_READ, 
                        &control_net_key);

  if (status != ERROR_SUCCESS)
    return status;

  for (i = 0; !stop; i++) {
    char enum_name[256];
    char connection_string[256];
    HKEY connKey;
    char name_data[256];
    DWORD name_type;
    const char name_string[] = "Name";

    len = sizeof (enum_name);
    status = RegEnumKeyEx(control_net_key, 
                          i, 
                          enum_name, 
                          &len, 
                          NULL, 
                          NULL, 
                          NULL, 
                          NULL);

    if (status == ERROR_NO_MORE_ITEMS) 
      break;
    else if (status != ERROR_SUCCESS) 
      break;

    if (len != strlen(NETWORK_ADAPTER_GUID))
      continue;

    _snprintf(connection_string,
              sizeof(connection_string),
              "%s\\%s\\Connection",
              NETWORK_CONNECTIONS_KEY,
              enum_name);

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
                          connection_string,
                          0,
                          KEY_READ,
                          &connKey);

    if (status != ERROR_SUCCESS) 
      break;

    len = sizeof (name_data);
    status = RegQueryValueEx(connKey,
                             name_string,
                             NULL,
                             &name_type,
                             (uint8_t*) name_data,
                             &len);

    if (status == ERROR_FILE_NOT_FOUND)
      continue;
    if (status != ERROR_SUCCESS)
      break;

    if (name_type != REG_SZ) {
      status = !ERROR_SUCCESS;
      return status;
    }

    if (is_tap_win32_dev(enum_name)) {
      _snprintf(name, name_size, "%s", enum_name);
      if (actual_name) {
        if (strcmp(actual_name, "") != 0) {
          if (strcmp(name_data, actual_name) != 0) {
            RegCloseKey (connKey);
            ++i;
            continue;
          }
        }
        else
          _snprintf(actual_name, actual_name_size, "%s", name_data);
      }
      stop = 1;
    }
    RegCloseKey(connKey);
  }

  RegCloseKey (control_net_key);
  if (stop == 0)
    return -1;

  return 0;
}
#endif

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static uv_loop_t* loop;
static int step = 0;

static void after_write(uv_write_t* req, int status);
static void after_read(uv_stream_t*, ssize_t nread, const uv_buf_t* buf);
static void on_close(uv_handle_t* peer) {
  fprintf(stderr, "close %p\n", (void*) peer);
  fflush(stderr);
}

static void after_write(uv_write_t* req, int status) {
  write_req_t* wr;

  if (step > 10) {
    uv_stream_t *s = (uv_stream_t*) req->handle;
    uv_read_stop(s);
  }
  /* Free the read/write buffer and the request */
  wr = (write_req_t*) req;
  free(wr->buf.base);
  free(wr);

  step += 1;
  if (status == 0)
    return;

  fprintf(stderr,
          "uv_write error: %s - %s\n",
          uv_err_name(status),
          uv_strerror(status));
  fflush(stderr);
}

static void after_read(uv_stream_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf) {
  if (nread < 0) {
    ASSERT(nread == UV_EOF);

    free(buf->base);
    uv_close((uv_handle_t*) handle, on_close);
    return;
  }

  if (nread == 0) {
    free(buf->base);
    return;
  }

  if (nread > 20 && buf->len > 20) {
    if (buf->base[0] == 0x45) {
      uint8_t ip[4];
      write_req_t *wr;
      memcpy(ip, buf->base + 12, 4);
      memcpy(buf->base + 12, buf->base + 16, 4);
      memcpy(buf->base + 16, ip, 4);
      wr = (write_req_t*) malloc(sizeof (*wr));
      ASSERT(wr != NULL);
      memset(wr, 0, sizeof *wr);
      wr->buf = uv_buf_init(buf->base, nread);

      if (uv_write(&wr->req, handle, &wr->buf, 1, after_write)) {
        fprintf(stderr, "uv_write failed\n");
        fflush(stderr);
        abort();
      }
    }else{
      free(buf->base);
      return;
    }
  } else {
    fprintf(stderr, "data %p len:%d\n", buf->base,(int) buf->len);
    fflush(stderr);
  }
}

static void echo_alloc(uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static char dev_path[1024];
#ifdef WIN32
static char dev_name[1024];
#endif
static const char* tun_get_path() {
#ifdef __linux__
  strncpy(dev_path, "/dev/net/tun", sizeof(dev_path));
  return dev_path;
#elif defined(WIN32)
#define BUF_SZ 1024
  char guid[BUF_SZ] = {0};
  if (get_device_guid(guid, sizeof(guid), dev_name, sizeof(dev_name))) {
    fprintf(stderr,"You need install tap-windows "             \
                "(https://github.com/OpenVPN/tap-windows) " \
                "to do this test\n");
    fflush(stderr);
    return NULL;
  }
  snprintf(dev_path, sizeof(dev_path), "%s%s%s", USERMODEDEVICEDIR, guid, TAPSUFFIX);
  return dev_path;
#else
  fprintf(stderr, "Only support device test on linux or windows\n");
  fflush(stderr);
  return NULL;
#endif
}

static int tun_config(uv_device_t *device) {
  int r;
#ifdef __linux__
  struct ifreq ifr;
  uv_ioargs_t args = {0};
  int flags = IFF_TUN | IFF_NO_PI;
  args.arg = &ifr;

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = flags;
  strncpy(ifr.ifr_name, "tun0", IFNAMSIZ);

  r = uv_device_ioctl(device, TUNSETIFF, &args);
  system("ifconfig tun0 10.3.0.2 netmask 255.255.255.252 pointopoint 10.3.0.1");
#elif defined(WIN32)
#define P2P
  uv_ioargs_t ioarg = {0};
  uint32_t version[3];
#ifdef P2P
  uint32_t p2p[2];
#else
  uint32_t p2p[3];
#endif
  uint32_t enable = 1;
  char tmp[MAX_PATH];
  snprintf(tmp,
           sizeof(tmp),
           "%%windir%%\\system32\\netsh interface ip set address \"%s\"" \
           " static 10.3.0.2 255.255.255.0",
           dev_name);
  system(tmp);
  ioarg.input_len = sizeof(version);
  ioarg.input = (void*) version;
  ioarg.output_len = sizeof(version);
  ioarg.output = (void*) version;

  r = uv_device_ioctl(device, TAP_IOCTL_GET_VERSION, &ioarg);
  ASSERT(r >= 0);
  printf("version: %d.%d.%d\n", version[0], version[1], version[2]);
  {
    ULONG mtu;
    int len = sizeof(ULONG);
    if (DeviceIoControl(device->handle, TAP_IOCTL_GET_MTU,
                        &mtu, sizeof(mtu),
                        &mtu, sizeof(mtu), &len, NULL)) {
      printf("TAP-Windows MTU=%d\n", (int) mtu);
    }
  }

#ifdef P2P
  p2p[0] = inet_addr("10.3.0.2");
  p2p[1] = inet_addr("10.3.0.1");
#else
  p2p[0] = inet_addr("10.3.0.2");
  p2p[1] = inet_addr("10.3.0.0");
  p2p[2] = inet_addr("255.255.255.0");
#endif

  ioarg.input_len = sizeof(p2p);
  ioarg.input = (void*) &p2p;
  ioarg.output_len = sizeof(p2p);
  ioarg.output = (void*) &p2p;
#ifdef P2P
  r = uv_device_ioctl(device, TAP_IOCTL_CONFIG_POINT_TO_POINT, &ioarg);
#else
  r = uv_device_ioctl(device, TAP_IOCTL_CONFIG_TUN, &ioarg);
#endif
  ASSERT(r >= 0);

  ioarg.input_len = sizeof(enable);
  ioarg.input = (void*) &enable;
  ioarg.output_len = sizeof(enable);
  ioarg.output = (void*) &enable;
  r = uv_device_ioctl(device, TAP_IOCTL_SET_MEDIA_STATUS, &ioarg);
#endif
  return r;
}

static void launch_ping() {
  uv_process_t child_req = {0};
  uv_process_options_t options = {0};
  uv_stdio_container_t child_stdio[3];
  char* args[5];

  args[0] = "ping";
  args[1] = "10.3.0.1";
#if WIN32
  args[2] = "-n";
#else
  args[2] = "-c";
#endif
  args[3] = "10";
  args[4] = NULL;

  options.exit_cb = NULL;
  options.file = "ping";
  options.args = args;
  options.stdio_count = 3;

  child_stdio[0].flags = UV_IGNORE;
  child_stdio[1].flags = UV_INHERIT_FD;
  child_stdio[1].data.fd = fileno(stdout);
  child_stdio[2].flags = UV_INHERIT_FD;
  child_stdio[2].data.fd = fileno(stderr);
  options.stdio = child_stdio;

  if (uv_spawn(loop, &child_req, &options)) {
    fprintf(stderr, "uv_spawn ping fail\n");
    fflush(stderr);
  }
  uv_unref((uv_handle_t*) &child_req);
}

TEST_IMPL(device_tun_echo) {
  uv_device_t device;
  const char* path = tun_get_path();
  int r;
  if (!path)
    return TEST_SKIP;

  loop = uv_default_loop();
  memset(&device, 0, sizeof(device));
  r = uv_device_init(loop, &device, path, O_RDWR);
  ASSERT(r == 0);

  r = tun_config(&device);
  ASSERT(r >= 0);

  launch_ping();

  r = uv_read_start((uv_stream_t*) &device, echo_alloc, after_read);
  ASSERT(r == 0);

  uv_run(loop, UV_RUN_DEFAULT);
  return 0;
}
