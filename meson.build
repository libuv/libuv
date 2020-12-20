project('uv', 'c',
  default_options: [
    'b_lto=true',
    'b_ndebug=if-release',
    'buildtype=debugoptimized',
    'c_std=c89',
    'warning_level=3',
  # 'werror=true',
  ],
  license: 'MIT',
  version: '1.40.0-development',
)

cc = meson.get_compiler('c')
target = target_machine.system()

add_project_arguments(cc.get_supported_arguments([
  '-Wno-long-long',        # relax strict ANSI C89 / ISO C90
  '-Wno-unused-parameter', # do not spam output

  '-Wno-error=declaration-after-statement', # FIXME
]), language: 'c')

uv_dep_cargs = []
uv_lib_cargs = [
  # https://github.com/libuv/libuv/commit/cbcd0cfc2
  cc.get_supported_arguments('-fno-strict-aliasing'),
  cc.get_supported_arguments('-fvisibility=hidden'),
]
uv_lib_largs = []
uv_deps = []
uv_src = []

# Neither AIX, nor OS390, nor OS400 nor QNX

if target == 'windows'
  if cc.get_id() == 'msvc' and get_option('default_library') == 'shared'
    uv_dep_cargs += '-DUSING_UV_SHARED'
    uv_lib_cargs += '-DBUILDING_UV_SHARED'
  endif
  uv_lib_cargs += ['-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0600']
  # Are AdvAPI32 and User32 required?
  uv_deps += [
    cc.find_library('Psapi'),
    cc.find_library('UserEnv'),
    cc.find_library('WS2_32'),
    cc.find_library('iphlpapi'),
  ]
elif target == 'darwin'
  uv_lib_cargs += ['-D_DARWIN_UNLIMITED_SELECT=1', '-D_DARWIN_USE_64_BIT_INODE=1']
elif target == 'freebsd'
  # ld: error: undefined symbol: environ
  # See https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1306#note_677553
  #  The reason that environ can't be found is that it doesn't exist in
  #  libc.so.7. Instead, environ is defined in crt1.o, which is only pulled in
  #  when linking an executable. This is not a new problem. You can see the same
  #  problem in an old bug report: https://bugs.webkit.org/show_bug.cgi?id=138420
  larg = '-Wl,--warn-unresolved-symbols'
  if get_option('default_library') != 'static' and cc.has_link_argument(larg)
    uv_lib_largs += larg
  endif
elif target == 'haiku'
  uv_lib_cargs += '-D_BSD_SOURCE'
  uv_deps += [dependency('bsd'), dependency('network')]
elif target == 'linux'
  uv_lib_cargs += ['-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200112UL']
  uv_deps += [
    cc.find_library('dl', required: false),
    cc.find_library('m', required: false),
  ]
elif target == 'netbsd'
  uv_deps += dependency('kvm')
elif target == 'sunos'
  uv_lib_cargs += ['-D_XOPEN_SOURCE=500', '-D__EXTENSIONS__']
  uv_deps += [
    dependency('kstat'),
    dependency('nsl'),
    dependency('sendfile'),
    dependency('socket'),
  ]
endif

if target != 'android'
  uv_deps += dependency('threads')
endif

uv_src_all = [
  ['unix/android-ifaddrs.c', 'android'],
  ['unix/async.c', '!win'],
  ['unix/bsd-ifaddrs.c', 'darwin dragonfly freebsd haiku netbsd openbsd'],
  ['unix/bsd-proctitle.c', 'dragonfly freebsd netbsd openbsd'],
  ['unix/core.c', '!win'],
  ['unix/darwin-proctitle', 'darwin'],
  ['unix/darwin.c', 'darwin'],
  ['unix/dl.c', '!win'],
  ['unix/freebsd.c', 'dragonfly freebsd'],
  ['unix/fs.c', '!win'],
  ['unix/fsevents.c', 'darwin'],
  ['unix/getaddrinfo.c', '!win'],
  ['unix/getnameinfo.c', '!win'],
  ['unix/kqueue.c', 'darwin dragonfly freebsd netbsd openbsd'],
  ['unix/linux-core.c', 'android linux'],
  ['unix/linux-inotify.c', 'android linux'],
  ['unix/linux-syscalls.c', 'android linux'],
  ['unix/loop-watcher.c', '!win'],
  ['unix/loop.c', '!win'],
  ['unix/netbsd.c', 'netbsd'],
  ['unix/no-fsevents.c', 'darwin'],
  ['unix/no-proctitle.c', 'haiku sunos'],
  ['unix/openbsd.c', 'openbsd'],
  ['unix/pipe.c', '!win'],
  ['unix/poll.c', '!win'],
  ['unix/posix-poll.c', 'darwin'],
  ['unix/posix-hrtime.c', 'dragonfly freebsd haiku netbsd openbsd'],
  ['unix/process.c', '!win'],
  ['unix/procfs-exepath.c', 'android linux'],
  ['unix/proctitle.c', 'android darwin linux'],
  ['unix/pthread-fixes.c', 'android'],
  ['unix/random-devurandom.c', '!win'],
  ['unix/random-getentropy.c', 'android darwin openbsd'],
  ['unix/random-getrandom.c', 'android freebsd linux'],
  ['unix/random-sysctl-linux.c', 'android linux'],
  ['unix/signal.c', '!win'],
  ['unix/stream.c', '!win'],
  ['unix/sunos.c', 'sunos'],
  ['unix/tcp.c', '!win'],
  ['unix/thread.c', '!win'],
  ['unix/tty.c', '!win'],
  ['unix/udp.c', '!win'],
  ['win/async.c', 'win'],
  ['win/core.c', 'win'],
  ['win/detect-wakeup.c', 'win'],
  ['win/dl.c', 'win'],
  ['win/error.c', 'win'],
  ['win/fs.c', 'win'],
  ['win/fs-event.c', 'win'],
  ['win/getaddrinfo.c', 'win'],
  ['win/getnameinfo.c', 'win'],
  ['win/handle.c', 'win'],
  ['win/loop-watcher.c', 'win'],
  ['win/pipe.c', 'win'],
  ['win/thread.c', 'win'],
  ['win/poll.c', 'win'],
  ['win/process.c', 'win'],
  ['win/process-stdio.c', 'win'],
  ['win/signal.c', 'win'],
  ['win/snprintf.c', 'win'],
  ['win/stream.c', 'win'],
  ['win/tcp.c', 'win'],
  ['win/tty.c', 'win'],
  ['win/udp.c', 'win'],
  ['win/util.c', 'win'],
  ['win/winapi.c', 'win'],
  ['win/winsock.c', 'win'],
  ['fs-poll.c'],
  ['idna.c'],
  ['inet.c'],
  ['random.c'],
  ['strscpy.c'],
  ['threadpool.c'],
  ['timer.c'],
  ['uv-common.c'],
  ['uv-data-getter-setters.c'],
  ['version.c'],
]

table = {}
if target == 'windows'
  table += {'win': true}
else
  table += {'!win': true, target: true}
endif
summary(table, section: 'System table')

foreach i : uv_src_all
  if i.length() > 1
    foreach j : i[1].split()
      if j in table
        uv_src += 'src' / i[0]
        break
      endif
    endforeach
  else
    uv_src += 'src' / i[0]
  endif
endforeach
message(uv_src)
uv_src = files(uv_src)

uv_lib = library('uv', uv_src,
  c_args: uv_lib_cargs,
  dependencies: uv_deps,
  include_directories: include_directories('include', 'src'),
  link_args: uv_lib_largs,
  version: meson.project_version().split('-')[0],
)

uv_dep = declare_dependency(
  compile_args: uv_dep_cargs,
  include_directories: include_directories('include'),
  link_with: uv_lib,
)

if meson.is_subproject()
    subdir_done()
endif

install_subdir('include', install_dir: 'include', strip_directory: true)

pc = import('pkgconfig')
pc.generate(uv_lib,
  description: 'Cross-platform asynchronous I/O',
  extra_cflags: uv_dep_cargs,
  url: 'https://libuv.org',
  # This is WRONG! It should simply by “uv”
  name: 'libuv',
)

uv_test_deps = [uv_dep]
uv_test_src = files(
  'test/blackhole-server.c',
  'test/dns-server.c',
  'test/echo-server.c',
  'test/run-tests.c',
  'test/runner.c',
  'test/test-active.c',
  'test/test-async-null-cb.c',
  'test/test-async.c',
  'test/test-barrier.c',
  'test/test-callback-order.c',
  'test/test-callback-stack.c',
  'test/test-close-fd.c',
  'test/test-close-order.c',
  'test/test-condvar.c',
  'test/test-connect-unspecified.c',
  'test/test-connection-fail.c',
  'test/test-cwd-and-chdir.c',
  'test/test-default-loop-close.c',
  'test/test-delayed-accept.c',
  'test/test-dlerror.c',
  'test/test-eintr-handling.c',
  'test/test-embed.c',
  'test/test-emfile.c',
  'test/test-env-vars.c',
  'test/test-error.c',
  'test/test-fail-always.c',
  'test/test-fork.c',
  'test/test-fs-copyfile.c',
  'test/test-fs-event.c',
  'test/test-fs-fd-hash.c',
  'test/test-fs-open-flags.c',
  'test/test-fs-poll.c',
  'test/test-fs-readdir.c',
  'test/test-fs.c',
  'test/test-get-currentexe.c',
  'test/test-get-loadavg.c',
  'test/test-get-memory.c',
  'test/test-get-passwd.c',
  'test/test-getaddrinfo.c',
  'test/test-gethostname.c',
  'test/test-getnameinfo.c',
  'test/test-getsockname.c',
  'test/test-getters-setters.c',
  'test/test-gettimeofday.c',
  'test/test-handle-fileno.c',
  'test/test-homedir.c',
  'test/test-hrtime.c',
  'test/test-idle.c',
  'test/test-idna.c',
  'test/test-ip4-addr.c',
  'test/test-ip6-addr.c',
  'test/test-ipc-heavy-traffic-deadlock-bug.c',
  'test/test-ipc-send-recv.c',
  'test/test-ipc.c',
  'test/test-loop-alive.c',
  'test/test-loop-close.c',
  'test/test-loop-configure.c',
  'test/test-loop-handles.c',
  'test/test-loop-stop.c',
  'test/test-loop-time.c',
  'test/test-metrics.c',
  'test/test-multiple-listen.c',
  'test/test-mutexes.c',
  'test/test-osx-select.c',
  'test/test-pass-always.c',
  'test/test-ping-pong.c',
  'test/test-pipe-bind-error.c',
  'test/test-pipe-close-stdout-read-stdin.c',
  'test/test-pipe-connect-error.c',
  'test/test-pipe-connect-multiple.c',
  'test/test-pipe-connect-prepare.c',
  'test/test-pipe-getsockname.c',
  'test/test-pipe-pending-instances.c',
  'test/test-pipe-sendmsg.c',
  'test/test-pipe-server-close.c',
  'test/test-pipe-set-fchmod.c',
  'test/test-pipe-set-non-blocking.c',
  'test/test-platform-output.c',
  'test/test-poll-close-doesnt-corrupt-stack.c',
  'test/test-poll-close.c',
  'test/test-poll-closesocket.c',
  'test/test-poll-oob.c',
  'test/test-poll.c',
  'test/test-process-priority.c',
  'test/test-process-title-threadsafe.c',
  'test/test-process-title.c',
  'test/test-queue-foreach-delete.c',
  'test/test-random.c',
  'test/test-ref.c',
  'test/test-run-nowait.c',
  'test/test-run-once.c',
  'test/test-semaphore.c',
  'test/test-shutdown-close.c',
  'test/test-shutdown-eof.c',
  'test/test-shutdown-twice.c',
  'test/test-signal-multiple-loops.c',
  'test/test-signal-pending-on-close.c',
  'test/test-signal.c',
  'test/test-socket-buffer-size.c',
  'test/test-spawn.c',
  'test/test-stdio-over-pipes.c',
  'test/test-strscpy.c',
  'test/test-tcp-alloc-cb-fail.c',
  'test/test-tcp-bind-error.c',
  'test/test-tcp-bind6-error.c',
  'test/test-tcp-close-accept.c',
  'test/test-tcp-close-reset.c',
  'test/test-tcp-close-while-connecting.c',
  'test/test-tcp-close.c',
  'test/test-tcp-connect-error-after-write.c',
  'test/test-tcp-connect-error.c',
  'test/test-tcp-connect-timeout.c',
  'test/test-tcp-connect6-error.c',
  'test/test-tcp-create-socket-early.c',
  'test/test-tcp-flags.c',
  'test/test-tcp-oob.c',
  'test/test-tcp-open.c',
  'test/test-tcp-read-stop-start.c',
  'test/test-tcp-read-stop.c',
  'test/test-tcp-shutdown-after-write.c',
  'test/test-tcp-try-write-error.c',
  'test/test-tcp-try-write.c',
  'test/test-tcp-unexpected-read.c',
  'test/test-tcp-write-after-connect.c',
  'test/test-tcp-write-fail.c',
  'test/test-tcp-write-queue-order.c',
  'test/test-tcp-write-to-half-open-connection.c',
  'test/test-tcp-writealot.c',
  'test/test-test-macros.c',
  'test/test-thread-equal.c',
  'test/test-thread.c',
  'test/test-threadpool-cancel.c',
  'test/test-threadpool.c',
  'test/test-timer-again.c',
  'test/test-timer-from-check.c',
  'test/test-timer.c',
  'test/test-tmpdir.c',
  'test/test-tty-duplicate-key.c',
  'test/test-tty-escape-sequence-processing.c',
  'test/test-tty.c',
  'test/test-udp-alloc-cb-fail.c',
  'test/test-udp-bind.c',
  'test/test-udp-connect.c',
  'test/test-udp-create-socket-early.c',
  'test/test-udp-dgram-too-big.c',
  'test/test-udp-ipv6.c',
  'test/test-udp-mmsg.c',
  'test/test-udp-multicast-interface.c',
  'test/test-udp-multicast-interface6.c',
  'test/test-udp-multicast-join.c',
  'test/test-udp-multicast-join6.c',
  'test/test-udp-multicast-ttl.c',
  'test/test-udp-open.c',
  'test/test-udp-options.c',
  'test/test-udp-send-and-recv.c',
  'test/test-udp-send-hang-loop.c',
  'test/test-udp-send-immediate.c',
  'test/test-udp-send-unreachable.c',
  'test/test-udp-sendmmsg-error.c',
  'test/test-udp-try-send.c',
  'test/test-uname.c',
  'test/test-walk-handles.c',
  'test/test-watcher-cross-stop.c',
)

if target == 'windows'
  uv_test_deps += cc.find_library('WS2_32')
  uv_test_src += files('test/runner-win.c')
else
  if target != 'android'
    uv_test_deps += dependency('threads')
  endif
  if target in 'darwin dragonfly freebsd linux netbsd openbsd'.split()
    uv_test_deps += cc.find_library('util')
  endif
  uv_test_src += files('test/runner-unix.c')
endif

uv_test = executable('uv_test', uv_test_src,
  c_args: uv_lib_cargs,
  dependencies: uv_test_deps,
)

test('uv_test', uv_test, timeout: 300)
