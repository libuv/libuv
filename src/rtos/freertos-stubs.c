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

/* Stubs for functions the FreeRTOS port does not implement. Functions that
 * return an error code return UV_ENOTSUP. Functions that can't report errors
 * return a neutral value (0, NULL, UV_UNKNOWN_HANDLE) or do nothing.
 */

#include "uv.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>  /* NULL */

int uv_accept(uv_stream_t* server, uv_stream_t* client) {
  return UV_ENOTSUP;
}

int uv_cancel(uv_req_t* req) {
  return UV_ENOTSUP;
}

int uv_chdir(const char* dir) {
  return UV_ENOTSUP;
}

int uv_clock_gettime(uv_clock_id clock_id, uv_timespec64_t* ts) {
  return UV_ENOTSUP;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  return UV_ENOTSUP;
}

int uv_cpumask_size(void) {
  return UV_ENOTSUP;
}

int uv_cwd(char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

void uv_disable_stdio_inheritance(void) {
}

void uv_dlclose(uv_lib_t* lib) {
}

const char* uv_dlerror(const uv_lib_t* lib) {
  return "dynamic libraries are not supported";
}

int uv_dlopen(const char* filename, uv_lib_t* lib) {
  return UV_ENOTSUP;
}

int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  return UV_ENOTSUP;
}

int uv_exepath(char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_fileno(const uv_handle_t* handle, uv_os_fd_t* fd) {
  return UV_ENOTSUP;
}

void uv_freeaddrinfo(uv_addrinfo_t* ai) {
}

int uv_fs_access(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
                 uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_chmod(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
                uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_chown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid,
                uv_gid_t gid, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_closedir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_copyfile(uv_loop_t* loop, uv_fs_t* req, const char* path,
                   const char* new_path, int flags, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  return UV_ENOTSUP;
}

int uv_fs_event_start(uv_fs_event_t* handle, uv_fs_event_cb cb,
                      const char* path, unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_fs_event_stop(uv_fs_event_t* handle) {
  return UV_ENOTSUP;
}

int uv_fs_fchmod(uv_loop_t* loop, uv_fs_t* req, uv_file file, int mode,
                 uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_fchown(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_uid_t uid,
                 uv_gid_t gid, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req, uv_file file,
                    int64_t offset, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_futime(uv_loop_t* loop, uv_fs_t* req, uv_file file, double atime,
                 double mtime, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_get_system_error(const uv_fs_t*) {
  return UV_ENOTSUP;
}

int uv_fs_lchown(uv_loop_t* loop, uv_fs_t* req, const char* path,
                 uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_link(uv_loop_t* loop, uv_fs_t* req, const char* path,
               const char* new_path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_lutime(uv_loop_t* loop, uv_fs_t* req, const char* path,
                 double atime, double mtime, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_mkdir(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
                uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_mkdtemp(uv_loop_t* loop, uv_fs_t* req,
                  const char* tpl, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_mkstemp(uv_loop_t* loop, uv_fs_t* req,
                  const char* tpl, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
               int mode, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_opendir(uv_loop_t* loop, uv_fs_t* req,
                  const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_poll_getpath(uv_fs_poll_t* handle, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_fs_poll_init(uv_loop_t* loop, uv_fs_poll_t* handle) {
  return UV_ENOTSUP;
}

int uv_fs_poll_start(uv_fs_poll_t* handle, uv_fs_poll_cb poll_cb,
                     const char* path, unsigned int interval) {
  return UV_ENOTSUP;
}

int uv_fs_poll_stop(uv_fs_poll_t* handle) {
  return UV_ENOTSUP;
}

int uv_fs_read(uv_loop_t* loop, uv_fs_t* req, uv_file file,
               const uv_buf_t bufs[], unsigned int nbufs, int64_t offset,
               uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_readdir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_readlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
                   uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_realpath(uv_loop_t* loop, uv_fs_t* req, const char* path,
                   uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_rename(uv_loop_t* loop, uv_fs_t* req, const char* path,
                 const char* new_path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

void uv_fs_req_cleanup(uv_fs_t* req) {
}

int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_scandir(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
                  uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent) {
  return UV_ENOTSUP;
}

int uv_fs_sendfile(uv_loop_t* loop, uv_fs_t* req, uv_file out_fd,
                   uv_file in_fd, int64_t in_offset, size_t length,
                   uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_statfs(uv_loop_t* loop, uv_fs_t* req,
                 const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_symlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
                  const char* new_path, int flags, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req,
                 const char* path, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_utime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime,
                double mtime, uv_fs_cb cb) {
  return UV_ENOTSUP;
}

int uv_fs_write(uv_loop_t* loop, uv_fs_t* req, uv_file file,
                const uv_buf_t bufs[], unsigned int nbufs, int64_t offset,
                uv_fs_cb cb) {
  return UV_ENOTSUP;
}

uint64_t uv_get_available_memory(void) {
  return 0;
}

uint64_t uv_get_constrained_memory(void) {
  return 0;
}

uint64_t uv_get_free_memory(void) {
  return 0;
}

uv_os_fd_t uv_get_osfhandle(int fd) {
  return fd;
}

int uv_get_process_title(char* buffer, size_t size) {
  return UV_ENOTSUP;
}

uint64_t uv_get_total_memory(void) {
  return 0;
}

int uv_getaddrinfo(uv_loop_t* loop, uv_getaddrinfo_t* req,
                   uv_getaddrinfo_cb getaddrinfo_cb, const char* node,
                   const char* service, const uv_addrinfo_t* hints) {
  return UV_ENOTSUP;
}

int uv_getnameinfo(uv_loop_t* loop, uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const uv_sockaddr_t* addr, int flags) {
  return UV_ENOTSUP;
}

int uv_getrusage(uv_rusage_t* rusage) {
  return UV_ENOTSUP;
}

int uv_getrusage_thread(uv_rusage_t* rusage) {
  return UV_ENOTSUP;
}

int uv_gettimeofday(uv_timeval64_t* tv) {
  return UV_ENOTSUP;
}

uv_handle_type uv_guess_handle(uv_file file) {
  return UV_UNKNOWN_HANDLE;
}

int uv_if_indextoiid(unsigned int ifindex, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_if_indextoname(unsigned int ifindex, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_inet_ntop(int af, const void* src, char* dst, size_t size) {
  return UV_ENOTSUP;
}

int uv_inet_pton(int af, const char* src, void* dst) {
  return UV_ENOTSUP;
}

int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  return UV_ENOTSUP;
}

int uv_ip4_addr(const char* ip, int port, uv_sockaddr_in_t* addr) {
  return UV_ENOTSUP;
}

int uv_ip4_name(const uv_sockaddr_in_t* src, char* dst, size_t size) {
  return UV_ENOTSUP;
}

int uv_ip6_addr(const char* ip, int port, uv_sockaddr_in6_t* addr) {
  return UV_ENOTSUP;
}

int uv_ip6_name(const uv_sockaddr_in6_t* src, char* dst, size_t size) {
  return UV_ENOTSUP;
}

int uv_ip_name(const uv_sockaddr_t* src, char* dst, size_t size) {
  return UV_ENOTSUP;
}

int uv_is_readable(const uv_stream_t* handle) {
  return 0;
}

int uv_is_writable(const uv_stream_t* handle) {
  return 0;
}

int uv_kill(int pid, int signum) {
  return UV_ENOTSUP;
}

int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb) {
  return UV_ENOTSUP;
}

void uv_loadavg(double avg[3]) {
  avg[0] = avg[1] = avg[2] = 0;
}

int uv_open_osfhandle(uv_os_fd_t os_fd) {
  return UV_ENOTSUP;
}

int uv_os_environ(uv_env_item_t** envitems, int* count) {
  return UV_ENOTSUP;
}

int uv_os_get_group(uv_group_t* grp, uv_uid_t gid) {
  return UV_ENOTSUP;
}

int uv_os_get_passwd(uv_passwd_t* pwd) {
  return UV_ENOTSUP;
}

int uv_os_get_passwd2(uv_passwd_t* pwd, uv_uid_t uid) {
  return UV_ENOTSUP;
}

int uv_os_getenv(const char* name, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_os_gethostname(char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

uv_pid_t uv_os_getpid(void) {
  return 0;
}

uv_pid_t uv_os_getppid(void) {
  return 0;
}

int uv_os_getpriority(uv_pid_t pid, int* priority) {
  return UV_ENOTSUP;
}

int uv_os_homedir(char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_os_setenv(const char* name, const char* value) {
  return UV_ENOTSUP;
}

int uv_os_setpriority(uv_pid_t pid, int priority) {
  return UV_ENOTSUP;
}

int uv_os_tmpdir(char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_os_uname(uv_utsname_t* buffer) {
  return UV_ENOTSUP;
}

int uv_os_unsetenv(const char* name) {
  return UV_ENOTSUP;
}

int uv_pipe(uv_file fds[2], int read_flags, int write_flags) {
  return UV_ENOTSUP;
}

int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
  return UV_ENOTSUP;
}

int uv_pipe_bind2(uv_pipe_t* handle, const char* name, size_t namelen,
                  unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_pipe_chmod(uv_pipe_t* handle, int flags) {
  return UV_ENOTSUP;
}

void uv_pipe_connect(uv_connect_t* req, uv_pipe_t* handle, const char* name,
                     uv_connect_cb cb) {
}

int uv_pipe_connect2(uv_connect_t* req, uv_pipe_t* handle, const char* name,
                     size_t namelen, unsigned int flags, uv_connect_cb cb) {
  return UV_ENOTSUP;
}

int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return UV_ENOTSUP;
}

int uv_pipe_init(uv_loop_t*, uv_pipe_t* handle, int ipc) {
  return UV_ENOTSUP;
}

int uv_pipe_open(uv_pipe_t*, uv_file file) {
  return UV_ENOTSUP;
}

int uv_pipe_pending_count(uv_pipe_t* handle) {
  return 0;
}

void uv_pipe_pending_instances(uv_pipe_t* handle, int count) {
}

uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle) {
  return UV_UNKNOWN_HANDLE;
}

int uv_poll_init(uv_loop_t* loop, uv_poll_t* handle, int fd) {
  return UV_ENOTSUP;
}

int uv_poll_init_socket(uv_loop_t* loop, uv_poll_t* handle,
                        uv_os_sock_t socket) {
  return UV_ENOTSUP;
}

int uv_poll_start(uv_poll_t* handle, int events, uv_poll_cb cb) {
  return UV_ENOTSUP;
}

int uv_poll_stop(uv_poll_t* handle) {
  return UV_ENOTSUP;
}

int uv_process_kill(uv_process_t*, int signum) {
  return UV_ENOTSUP;
}

int uv_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  return UV_ENOTSUP;
}

int uv_random(uv_loop_t* loop, uv_random_t* req, void *buf, size_t buflen,
              unsigned flags, uv_random_cb cb) {
  return UV_ENOTSUP;
}

int uv_read_stop(uv_stream_t*) {
  return UV_ENOTSUP;
}

int uv_recv_buffer_size(uv_handle_t* handle, int* value) {
  return UV_ENOTSUP;
}

int uv_resident_set_memory(size_t* rss) {
  return UV_ENOTSUP;
}

int uv_send_buffer_size(uv_handle_t* handle, int* value) {
  return UV_ENOTSUP;
}

int uv_set_process_title(const char* title) {
  return UV_ENOTSUP;
}

char** uv_setup_args(int argc, char** argv) {
  return argv;
}

int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle) {
  return UV_ENOTSUP;
}

int uv_signal_start(uv_signal_t* handle, uv_signal_cb signal_cb, int signum) {
  return UV_ENOTSUP;
}

int uv_signal_start_oneshot(uv_signal_t* handle, uv_signal_cb signal_cb,
                            int signum) {
  return UV_ENOTSUP;
}

int uv_signal_stop(uv_signal_t* handle) {
  return UV_ENOTSUP;
}

void uv_sleep(unsigned int msec) {
  vTaskDelay(pdMS_TO_TICKS(msec));
}

int uv_socketpair(int type, int protocol, uv_os_sock_t socket_vector[2],
                  int flags0, int flags1) {
  return UV_ENOTSUP;
}

int uv_spawn(uv_loop_t* loop, uv_process_t* handle,
             const uv_process_options_t* options) {
  return UV_ENOTSUP;
}

int uv_stream_set_blocking(uv_stream_t* handle, int blocking) {
  return UV_ENOTSUP;
}

int uv_tcp_bind(uv_tcp_t* handle, const uv_sockaddr_t* addr,
                unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
  return UV_ENOTSUP;
}

int uv_tcp_connect(uv_connect_t* req, uv_tcp_t* handle,
                   const uv_sockaddr_t* addr, uv_connect_cb cb) {
  return UV_ENOTSUP;
}

int uv_tcp_getpeername(const uv_tcp_t* handle, uv_sockaddr_t* name,
                       int* namelen) {
  return UV_ENOTSUP;
}

int uv_tcp_getsockname(const uv_tcp_t* handle, uv_sockaddr_t* name,
                       int* namelen) {
  return UV_ENOTSUP;
}

int uv_tcp_init(uv_loop_t*, uv_tcp_t* handle) {
  return UV_ENOTSUP;
}

int uv_tcp_init_ex(uv_loop_t*, uv_tcp_t* handle, unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_tcp_keepalive(uv_tcp_t* handle, int enable, unsigned int delay) {
  return UV_ENOTSUP;
}

int uv_tcp_keepalive_ex(uv_tcp_t* handle, int on, unsigned int idle,
                        unsigned int intvl, unsigned int cnt) {
  return UV_ENOTSUP;
}

int uv_tcp_nodelay(uv_tcp_t* handle, int enable) {
  return UV_ENOTSUP;
}

int uv_tcp_open(uv_tcp_t* handle, uv_os_sock_t sock) {
  return UV_ENOTSUP;
}

int uv_tcp_simultaneous_accepts(uv_tcp_t* handle, int enable) {
  return UV_ENOTSUP;
}

int uv_thread_getpriority(uv_thread_t tid, int* priority) {
  return UV_ENOTSUP;
}

int uv_thread_setpriority(uv_thread_t tid, int priority) {
  return UV_ENOTSUP;
}

int uv_translate_sys_error(int sys_errno) {
  return UV_ENOTSUP;
}

int uv_try_write(uv_stream_t* handle, const uv_buf_t bufs[],
                 unsigned int nbufs) {
  return UV_ENOTSUP;
}

int uv_try_write2(uv_stream_t* handle, const uv_buf_t bufs[],
                  unsigned int nbufs, uv_stream_t* send_handle) {
  return UV_ENOTSUP;
}

int uv_tty_get_vterm_state(uv_tty_vtermstate_t* state) {
  return UV_ENOTSUP;
}

int uv_tty_get_winsize(uv_tty_t*, int* width, int* height) {
  return UV_ENOTSUP;
}

int uv_tty_init(uv_loop_t*, uv_tty_t*, uv_file fd, int readable) {
  return UV_ENOTSUP;
}

int uv_tty_reset_mode(void) {
  return UV_ENOTSUP;
}

int uv_tty_set_mode(uv_tty_t*, uv_tty_mode_t mode) {
  return UV_ENOTSUP;
}

void uv_tty_set_vterm_state(uv_tty_vtermstate_t state) {
}

int uv_udp_bind(uv_udp_t* handle, const uv_sockaddr_t* addr,
                unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_udp_connect(uv_udp_t* handle, const uv_sockaddr_t* addr) {
  return UV_ENOTSUP;
}

int uv_udp_getpeername(const uv_udp_t* handle, uv_sockaddr_t* name,
                       int* namelen) {
  return UV_ENOTSUP;
}

int uv_udp_getsockname(const uv_udp_t* handle, uv_sockaddr_t* name,
                       int* namelen) {
  return UV_ENOTSUP;
}

int uv_udp_init(uv_loop_t*, uv_udp_t* handle) {
  return UV_ENOTSUP;
}

int uv_udp_init_ex(uv_loop_t*, uv_udp_t* handle, unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock) {
  return UV_ENOTSUP;
}

int uv_udp_open_ex(uv_udp_t* handle, uv_os_sock_t sock, unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_udp_recv_start(uv_udp_t* handle, uv_alloc_cb alloc_cb,
                      uv_udp_recv_cb recv_cb) {
  return UV_ENOTSUP;
}

int uv_udp_recv_stop(uv_udp_t* handle) {
  return UV_ENOTSUP;
}

int uv_udp_send(uv_udp_send_t* req, uv_udp_t* handle, const uv_buf_t bufs[],
                unsigned int nbufs, const uv_sockaddr_t* addr,
                uv_udp_send_cb send_cb) {
  return UV_ENOTSUP;
}

int uv_udp_set_broadcast(uv_udp_t* handle, int on) {
  return UV_ENOTSUP;
}

int uv_udp_set_membership(uv_udp_t* handle, const char* multicast_addr,
                          const char* interface_addr,
                          uv_membership membership) {
  return UV_ENOTSUP;
}

int uv_udp_set_multicast_interface(uv_udp_t* handle,
                                   const char* interface_addr) {
  return UV_ENOTSUP;
}

int uv_udp_set_multicast_loop(uv_udp_t* handle, int on) {
  return UV_ENOTSUP;
}

int uv_udp_set_multicast_ttl(uv_udp_t* handle, int ttl) {
  return UV_ENOTSUP;
}

int uv_udp_set_source_membership(uv_udp_t* handle, const char* multicast_addr,
                                 const char* interface_addr,
                                 const char* source_addr,
                                 uv_membership membership) {
  return UV_ENOTSUP;
}

int uv_udp_set_ttl(uv_udp_t* handle, int ttl) {
  return UV_ENOTSUP;
}

int uv_udp_try_send(uv_udp_t* handle, const uv_buf_t bufs[],
                    unsigned int nbufs, const uv_sockaddr_t* addr) {
  return UV_ENOTSUP;
}

int uv_udp_try_send2(uv_udp_t* handle, unsigned int count, uv_buf_t* bufs[],
                     unsigned int nbufs[], uv_sockaddr_t* addrs[],
                     unsigned int flags) {
  return UV_ENOTSUP;
}

int uv_udp_using_recvmmsg(const uv_udp_t* handle) {
  return 0;
}

int uv_uptime(double* uptime) {
  return UV_ENOTSUP;
}

int uv_write(uv_write_t* req, uv_stream_t* handle, const uv_buf_t bufs[],
             unsigned int nbufs, uv_write_cb cb) {
  return UV_ENOTSUP;
}

int uv_write2(uv_write_t* req, uv_stream_t* handle, const uv_buf_t bufs[],
              unsigned int nbufs, uv_stream_t* send_handle, uv_write_cb cb) {
  return UV_ENOTSUP;
}

size_t uv_write_nwritten(const uv_write_t* req) {
  return 0;
}
