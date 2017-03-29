#include "uv.h"
#include <sys/types.h>
#include <sys/sysctl.h>

int uv__tcp_enable_largesocket(uv_tcp_t *tcp) {
  int rmem;
  int wmem;
  int r;

  size_t size = sizeof(int);
  if (sysctlbyname("net.inet.tcp.recvspace", &rmem, &size, NULL, 0) ||
      rmem <= STDTCPWINDOW)
    return UV_ENOSYS;
  
  if (sysctlbyname("net.inet.tcp.sendspace", &wmem, &size, NULL, 0) ||
      wmem <= STDTCPWINDOW)
    return UV_ENOSYS;

  r = uv_recv_buffer_size((uv_handle_t*) tcp, &rmem);
  if (r != 0)
    return UV_ENOSYS;

  r = uv_send_buffer_size((uv_handle_t*) tcp, &wmem);
  if (r != 0)
    return UV_ENOSYS;

  tcp->chunk_read_size = rmem;
  return 0;
}
