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
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
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

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1, METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2, METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3, METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7, METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9, METHOD_BUFFERED)

static int is_tap_win32_dev(const char *guid) {
  HKEY netcard_key;
  LONG status;
  DWORD len;
  int i = 0;

  status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
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
  LONG status;
  HKEY control_net_key;
  DWORD len;
  int stop = 0;
  int i;

  status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
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
        else {
          _snprintf(actual_name, actual_name_size, "%s", name_data);
        }
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

const char* TAPDevice_find(char* preferredName,
                           int nlen,
                           char* buffguid,
                           int len) {
  if (get_device_guid(buffguid, len, preferredName, nlen)) {
    return NULL;
  }
  return buffguid;
}

#endif

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static uv_loop_t* loop;

static void after_write(uv_write_t* req, int status);
static void after_read(uv_stream_t*, ssize_t nread, const uv_buf_t* buf);
static void on_close(uv_handle_t* peer);

static int step = 0;
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
}

static void after_shutdown(uv_shutdown_t* req, int status) {
  uv_close((uv_handle_t*) req->handle, on_close);
  free(req);
}

static void after_read(uv_stream_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf) {
  write_req_t *wr;

  if (nread < 0) {
    /* Error or EOF */
    ASSERT(nread == UV_EOF);

    free(buf->base);
    uv_close((uv_handle_t*) handle, on_close);
    return;
  }

  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf->base);
    return;
  }

  /*
   * Scan for the letter Q which signals that we should quit the server.
   * If we get QS it means close the stream.
   */
  ASSERT(nread>20);
  if (nread > 20 && buf->len > 20) {
    uint8_t ip[4];
    memcpy(ip,buf->base+12,4);
    memcpy(buf->base+12,buf->base+16,4);
    memcpy(buf->base+16,ip,4);
  } else {
    printf("data %p len:%d\n", buf->base,buf->len);
  }

  wr = (write_req_t*) malloc(sizeof *wr);
  ASSERT(wr != NULL);
  wr->buf = uv_buf_init(buf->base, nread);

  if (uv_write(&wr->req, handle, &wr->buf, 1, after_write)) {
    printf("uv_write failed\n");
    abort();
  }
}

static void on_close(uv_handle_t* peer) {
  printf("close %p\n", (void*) peer);
}

static void echo_alloc(uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

void at_exit(uv_process_t *req, int64_t exit_status, int term_signal) {
  fprintf(stderr, 
          "Process exited with status %d, signal %d\n", 
          exit_status, 
          term_signal);
  uv_close((uv_handle_t*) req, NULL);
}

TEST_IMPL(device_tun_echo) {
  #define BUF_SZ 1024
  uv_device_t device;
  char buff[BUF_SZ] = {0};
#ifdef WIN32
  char guid[BUF_SZ] = {0};
  char tmp[MAX_PATH];
#endif
  int r;

#ifdef __linux__
  strcpy(buff,"/dev/net/tun");
#else
#ifdef WIN32

  if (!TAPDevice_find(buff, sizeof(buff), guid, sizeof(guid)))
  {
    printf("You need install tap-windows "             \
           "(https://github.com/OpenVPN/tap-windows) " \
            "to do this test\n");
    return 0;
  }

  snprintf(tmp, 
           sizeof(tmp),
           "%%windir%%\\system32\\netsh interface ip set address \"%s\"" \
           " static 10.3.0.2 255.255.255.0",
           buff);
  system(tmp);

  snprintf(buff,sizeof(buff), "%s%s%s",USERMODEDEVICEDIR,guid,TAPSUFFIX);
#else
  printf("We not have test for uv_device_t on you platform, please wait\n");
  return 0;
#endif
#endif

  loop = uv_default_loop();

  r = uv_device_init(loop, &device, buff, O_RDWR);
  ASSERT(r == 0);

#ifdef __linux__
  {
    struct ifreq ifr;
    uv_ioargs_t args = {0};
    int flags = IFF_TUN|IFF_NO_PI;
    args.arg = &ifr;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    strncpy(ifr.ifr_name, "tun0", IFNAMSIZ);

    r = uv_device_ioctl(&device, TUNSETIFF, &args);
    ASSERT(r >= 0);

    /* should be use uv_spawn */
    if (fork() == 0) {
      system(
        "ifconfig tun0 10.3.0.1 netmask 255.255.255.252 pointopoint 10.3.0.2"
      ); 
      system("ping 10.3.0.2 -c 10"); 
      exit(0);
   }
  }
#endif
#ifdef WIN32
  {
    uv_process_t child_req = {0};
    uv_process_options_t options = {0};
    uv_stdio_container_t child_stdio[3];
    char* args[5];

    uv_ioargs_t ioarg = {0};
    uint32_t version[3];
    uint32_t p2p[2];
    uint32_t enable = 1;

    ioarg.input_len = sizeof(version);
    ioarg.input = (void*) version;
    ioarg.output_len = sizeof(version);
    ioarg.output = (void*) version;

    r = uv_device_ioctl(&device, TAP_IOCTL_GET_VERSION, &ioarg);
    ASSERT(r >= 0);
    printf("version: %d.%d.%d\n",version[0],version[1],version[2]);

    p2p[0] = inet_addr("10.3.0.2");
    p2p[1] = inet_addr("10.3.0.1");

    ioarg.input_len = sizeof(p2p);
    ioarg.input = (void*) &p2p;
    ioarg.output_len = sizeof(p2p);
    ioarg.output = (void*) &p2p;

    r = uv_device_ioctl(&device, TAP_IOCTL_CONFIG_POINT_TO_POINT, &ioarg);
    ASSERT(r >= 0);

    ioarg.input_len = sizeof(enable);
    ioarg.input = (void*) &enable;
    ioarg.output_len = sizeof(enable);
    ioarg.output = (void*) &enable;

    r = uv_device_ioctl(&device, TAP_IOCTL_SET_MEDIA_STATUS, &ioarg);
    ASSERT(r >= 0);

    args[0] = "ping";
    args[1] = "10.3.0.1";
    args[2] = "-n";
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
      return 1;
    }
    fprintf(stderr, "Launched ping with PID %d\n", child_req.pid);
    uv_unref((uv_handle_t*) &child_req);
  }
#endif

  r = uv_read_start((uv_stream_t*) &device, echo_alloc, after_read);
  ASSERT(r == 0);

  uv_run(loop, UV_RUN_DEFAULT);
  return 0;
}
