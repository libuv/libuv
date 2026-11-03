#ifdef __linux__

#include <linux/tcp.h>
#include <netinet/in.h>

int test_tcp_tfo_used(int sock) {
  struct tcp_info info;
  socklen_t infolen = sizeof(info);
  if (getsockopt(sock, IPPROTO_TCP, TCP_INFO, &info, &infolen))
    return -1;

  return !!(info.tcpi_options & TCPI_OPT_SYN_DATA)
          || info.tcpi_fastopen_client_fail != TFO_STATUS_UNSPEC;
}

#else

int test_tcp_tfo_used(int sock) {
  return -1;
}

#endif
