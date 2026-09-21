/* Copyright libuv project contributors. All rights reserved.
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

#include <string.h>

#if !defined(_WIN32)
# include <fcntl.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

#define EXPECTED_TOS 0x62  /* AF41 + ECT(0) */



TEST_IMPL(udp_send_opts_validate) {
  struct sockaddr_in dest;
  uv_udp_send_opts_t opts;
  const uv_udp_send_opts_t* opts1[1];
  struct sockaddr* addrs[1];
  unsigned int nbufs[1];
  uv_buf_t* bufs[1];
  uv_loop_t* loop;
  uv_udp_t h;
  uv_buf_t buf;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));

  buf = uv_buf_init("PING", 4);
  bufs[0] = &buf;
  nbufs[0] = 1;
  addrs[0] = (struct sockaddr*) &dest;
  opts1[0] = &opts;

  memset(&opts, 0, sizeof(opts));
  opts.flags = 0x100;
  ASSERT_EQ(UV_EINVAL, uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0));

  opts.flags = UV_UDP_SEND_TOS;
  opts.tos = 256;
  ASSERT_EQ(UV_EINVAL, uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0));

  opts.flags = UV_UDP_SEND_SEGMENT;
  opts.segment_size = 0;
  ASSERT_EQ(UV_EINVAL, uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0));

  opts.flags = UV_UDP_SEND_PKTINFO;
  opts.src.ss_family = AF_UNSPEC;
  ASSERT_EQ(UV_EINVAL, uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0));

#if defined(_WIN32)
  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_TOS;
  opts.tos = EXPECTED_TOS;
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &dest, 0));
  ASSERT_EQ(UV_ENOTSUP,
            uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0));
#endif

#if !defined(__linux__)
  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_SEGMENT;
  opts.segment_size = 1000;
  {
    static uv_udp_send_t sreq;
    ASSERT_EQ(UV_ENOTSUP,
              uv_udp_send2(&sreq, &h, &buf, 1, (const struct sockaddr*) &dest,
                           &opts, NULL));
  }
#endif

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


#if defined(__linux__) && !defined(__QEMU__)

/* Raw-socket receiver so the assertions are against the kernel's view,
 * independent of libuv's receive path.
 */
static int recv_fd_open(void) {
  struct sockaddr_in addr;
  int on;
  int fd;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(fd, 0);
  on = 1;
#if defined(IP_RECVTOS)
  ASSERT_OK(setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &on, sizeof(on)));
#endif
  ASSERT_OK(bind(fd, (const struct sockaddr*) &addr, sizeof(addr)));
  ASSERT_OK(fcntl(fd, F_SETFL, O_NONBLOCK));

  return fd;
}


static ssize_t recv_fd_one(int fd,
                           char* buf,
                           size_t len,
                           int* tos,
                           struct sockaddr_in* from) {
  char control[64];
  struct cmsghdr* cmsg;
  struct msghdr h;
  struct iovec iov;
  ssize_t r;

  iov.iov_base = buf;
  iov.iov_len = len;
  memset(&h, 0, sizeof(h));
  h.msg_iov = &iov;
  h.msg_iovlen = 1;
  h.msg_control = control;
  h.msg_controllen = sizeof(control);
  if (from != NULL) {
    h.msg_name = from;
    h.msg_namelen = sizeof(*from);
  }

  do
    r = recvmsg(fd, &h, 0);
  while (r == -1 && errno == EINTR);

  if (r < 0)
    return -1;

  if (tos != NULL) {
    *tos = -1;
    for (cmsg = CMSG_FIRSTHDR(&h); cmsg != NULL; cmsg = CMSG_NXTHDR(&h, cmsg)) {
      if (cmsg->cmsg_level != IPPROTO_IP)
        continue;
      if (cmsg->cmsg_type == IP_TOS
#if defined(IP_RECVTOS) && (IP_RECVTOS != IP_TOS)
          || cmsg->cmsg_type == IP_RECVTOS
#endif
          ) {
        if (cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
          memcpy(tos, CMSG_DATA(cmsg), sizeof(*tos));
        else
          *tos = *(unsigned char*) CMSG_DATA(cmsg);
      }
    }
  }

  return r;
}


static int send_cb_called;


static void send2_cb(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
  send_cb_called++;
  uv_close((uv_handle_t*) req->handle, NULL);
}


TEST_IMPL(udp_send_opts_tos) {
  struct sockaddr_in dest;
  struct sockaddr_in any;
  uv_udp_send_opts_t opts;
  const uv_udp_send_opts_t* opts1[1];
  struct sockaddr* addrs[1];
  unsigned int nbufs[1];
  uv_buf_t* bufs[1];
  uv_loop_t* loop;
  uv_udp_t h;
  uv_buf_t buf;
  char payload[64];
  int tos;
  int fd;
  int r;

  fd = recv_fd_open();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &any));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &any, 0));

  buf = uv_buf_init("PING", 4);
  bufs[0] = &buf;
  nbufs[0] = 1;
  addrs[0] = (struct sockaddr*) &dest;
  opts1[0] = &opts;

  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_TOS;
  opts.tos = EXPECTED_TOS;

  r = uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0);
  ASSERT_EQ(1, r);

  ASSERT_EQ(4, recv_fd_one(fd, payload, sizeof(payload), &tos, NULL));
  ASSERT_OK(memcmp(payload, "PING", 4));
  ASSERT_EQ(EXPECTED_TOS, tos);

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  close(fd);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_send_opts_send2) {
  struct sockaddr_in dest;
  struct sockaddr_in any;
  uv_udp_send_opts_t opts;
  static uv_udp_send_t req;
  uv_loop_t* loop;
  uv_udp_t h;
  uv_buf_t buf;
  char payload[64];
  int tos;
  int fd;

  fd = recv_fd_open();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &any));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &any, 0));

  buf = uv_buf_init("PING", 4);
  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_TOS;
  opts.tos = EXPECTED_TOS;

  ASSERT_OK(uv_udp_send2(&req, &h, &buf, 1, (const struct sockaddr*) &dest,
                         &opts, send2_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, send_cb_called);

  ASSERT_EQ(4, recv_fd_one(fd, payload, sizeof(payload), &tos, NULL));
  ASSERT_OK(memcmp(payload, "PING", 4));
  ASSERT_EQ(EXPECTED_TOS, tos);

  close(fd);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_send_opts_gso) {
  struct sockaddr_in dest;
  struct sockaddr_in any;
  uv_udp_send_opts_t opts;
  const uv_udp_send_opts_t* opts1[1];
  struct sockaddr* addrs[1];
  unsigned int nbufs[1];
  uv_buf_t* bufs[1];
  uv_loop_t* loop;
  uv_udp_t h;
  uv_buf_t buf;
  static char super_buffer[2500];
  char payload[4096];
  int fd;
  int r;

  fd = recv_fd_open();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &any));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &any, 0));

  memset(super_buffer, 42, sizeof(super_buffer));
  buf = uv_buf_init(super_buffer, sizeof(super_buffer));
  bufs[0] = &buf;
  nbufs[0] = 1;
  addrs[0] = (struct sockaddr*) &dest;
  opts1[0] = &opts;

  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_SEGMENT;
  opts.segment_size = 1000;

  r = uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0);
  if (r == UV_EIO || r == UV_EINVAL) {
    /* Kernel without UDP_SEGMENT support. */
    uv_close((uv_handle_t*) &h, NULL);
    ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
    close(fd);
    MAKE_VALGRIND_HAPPY(loop);
    RETURN_SKIP("UDP_SEGMENT not supported by this kernel");
  }
  ASSERT_EQ(1, r);

  /* The kernel splits the super-buffer on segment_size boundaries. */
  ASSERT_EQ(1000, recv_fd_one(fd, payload, sizeof(payload), NULL, NULL));
  ASSERT_EQ(1000, recv_fd_one(fd, payload, sizeof(payload), NULL, NULL));
  ASSERT_EQ(500, recv_fd_one(fd, payload, sizeof(payload), NULL, NULL));

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  close(fd);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_send_opts_batch) {
  struct sockaddr_in dest;
  struct sockaddr_in any;
  uv_udp_send_opts_t opts0;
  uv_udp_send_opts_t opts2;
  const uv_udp_send_opts_t* opts[3];
  struct sockaddr* addrs[3];
  unsigned int nbufs[3];
  uv_buf_t* bufs[3];
  uv_buf_t buf0;
  uv_buf_t buf1;
  uv_buf_t buf2;
  uv_loop_t* loop;
  uv_udp_t h;
  char payload[64];
  int tos;
  int fd;
  int r;

  fd = recv_fd_open();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &any));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &any, 0));

  /* Mixed batch through the sendmmsg branch: with, without, with options. */
  buf0 = uv_buf_init("ONE", 3);
  buf1 = uv_buf_init("TWO", 3);
  buf2 = uv_buf_init("THREE", 5);
  bufs[0] = &buf0;
  bufs[1] = &buf1;
  bufs[2] = &buf2;
  nbufs[0] = nbufs[1] = nbufs[2] = 1;
  addrs[0] = addrs[1] = addrs[2] = (struct sockaddr*) &dest;

  memset(&opts0, 0, sizeof(opts0));
  opts0.flags = UV_UDP_SEND_TOS;
  opts0.tos = 0x28;
  memset(&opts2, 0, sizeof(opts2));
  opts2.flags = UV_UDP_SEND_TOS;
  opts2.tos = EXPECTED_TOS;
  opts[0] = &opts0;
  opts[1] = NULL;
  opts[2] = &opts2;

  r = uv_udp_try_send3(&h, 3, bufs, nbufs, addrs, opts, 0);
  ASSERT_EQ(3, r);

  ASSERT_EQ(3, recv_fd_one(fd, payload, sizeof(payload), &tos, NULL));
  ASSERT_OK(memcmp(payload, "ONE", 3));
  ASSERT_EQ(0x28, tos);

  ASSERT_EQ(3, recv_fd_one(fd, payload, sizeof(payload), &tos, NULL));
  ASSERT_OK(memcmp(payload, "TWO", 3));
  ASSERT_OK(tos);

  ASSERT_EQ(5, recv_fd_one(fd, payload, sizeof(payload), &tos, NULL));
  ASSERT_OK(memcmp(payload, "THREE", 5));
  ASSERT_EQ(EXPECTED_TOS, tos);

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  close(fd);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_send_opts_pktinfo) {
  struct sockaddr_in dest;
  struct sockaddr_in any;
  struct sockaddr_in from;
  struct sockaddr_in expected;
  uv_udp_send_opts_t opts;
  const uv_udp_send_opts_t* opts1[1];
  struct sockaddr* addrs[1];
  unsigned int nbufs[1];
  uv_buf_t* bufs[1];
  uv_loop_t* loop;
  uv_udp_t h;
  uv_buf_t buf;
  char payload[64];
  int fd;
  int r;

  fd = recv_fd_open();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &dest));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &any));
  /* Any 127/8 address is local on Linux. */
  ASSERT_OK(uv_ip4_addr("127.0.0.2", 0, &expected));

  loop = uv_default_loop();
  ASSERT_OK(uv_udp_init(loop, &h));
  ASSERT_OK(uv_udp_bind(&h, (const struct sockaddr*) &any, 0));

  buf = uv_buf_init("PING", 4);
  bufs[0] = &buf;
  nbufs[0] = 1;
  addrs[0] = (struct sockaddr*) &dest;
  opts1[0] = &opts;

  memset(&opts, 0, sizeof(opts));
  opts.flags = UV_UDP_SEND_PKTINFO;
  memcpy(&opts.src, &expected, sizeof(expected));

  r = uv_udp_try_send3(&h, 1, bufs, nbufs, addrs, opts1, 0);
  ASSERT_EQ(1, r);

  memset(&from, 0, sizeof(from));
  ASSERT_EQ(4, recv_fd_one(fd, payload, sizeof(payload), NULL, &from));
  ASSERT_EQ(AF_INET, from.sin_family);
  ASSERT_OK(memcmp(&from.sin_addr,
                   &expected.sin_addr,
                   sizeof(expected.sin_addr)));

  uv_close((uv_handle_t*) &h, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  close(fd);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

#else

TEST_IMPL(udp_send_opts_tos) {
  RETURN_SKIP("Linux-only test");
}


TEST_IMPL(udp_send_opts_send2) {
  RETURN_SKIP("Linux-only test");
}


TEST_IMPL(udp_send_opts_gso) {
  RETURN_SKIP("Linux-only test");
}


TEST_IMPL(udp_send_opts_batch) {
  RETURN_SKIP("Linux-only test");
}


TEST_IMPL(udp_send_opts_pktinfo) {
  RETURN_SKIP("Linux-only test");
}

#endif  /* defined(__linux__) && !defined(__QEMU__) */
