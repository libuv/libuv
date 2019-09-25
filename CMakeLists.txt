# TODO: determine CMAKE_SYSTEM_NAME on OS/390.  Currently assumes "OS/390".
cmake_minimum_required(VERSION 3.0)
project(libuv LANGUAGES C)

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(CTest)

find_package(Threads)

cmake_dependent_option(UV_BUILD_TESTS
  "Build libuv unit tests" ON
  "BUILD_TESTING;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

# Generator expression queries to make it easier to read later on
set(is-clang $<OR:$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:Clang>>)
set(is-msvc $<C_COMPILER_ID:MSVC>)
set(is-gcc $<C_COMPILER_ID:GNU>)

set(is-clang-or-gcc $<OR:${is-clang},${is-gcc}>)

set(c-standard $<AND:$<VERSION_LESS:${CMAKE_VERSION},3.1>,${is-clang-or-gcc}>)

set(is-dragonfly $<PLATFORM_ID:DragonFly>)
set(is-freebsd $<PLATFORM_ID:FreeBSD>)
set(is-openbsd $<PLATFORM_ID:OpenBSD>)
set(is-netbsd $<PLATFORM_ID:NetBSD>)
set(is-darwin $<PLATFORM_ID:Darwin>)

set(is-android $<PLATFORM_ID:Android>)
set(is-os/390 $<PLATFORM_ID:OS/390>)
set(is-linux $<PLATFORM_ID:Linux>)
set(is-sunos $<PLATFORM_ID:SunOS>)
set(is-aix $<PLATFORM_ID:AIX>)

set(is-windows $<PLATFORM_ID:Windows>)
set(is-unix $<BOOL:${UNIX}>)

set(is-not-darwin-bsd $<OR:${is-dragonfly},${is-freebsd},${is-openbsd},${is-netbsd}>)
set(is-bsd $<OR:${is-darwin},${is-not-darwin-bsd}>)

set(uv-sources
  src/fs-poll.c
  src/idna.c
  src/inet.c
  src/loop-watcher.c
  src/strscpy.c
  src/threadpool.c
  src/timer.c
  src/uv-common.c
  src/uv-data-getter-setters.c
  src/version.c

  $<$<OR:${is-darwin},${is-android},${is-linux},${is-os/390}>:src/unix/proctitle.c>
  $<$<OR:${is-dragonfly},${is-freebsd}>:src/unix/freebsd.c>
  $<${is-not-darwin-bsd}:src/unix/bsd-proctitle.c>
  $<${is-not-darwin-bsd}:src/unix/posix-hrtime.c>

  $<${is-linux}:src/unix/sysinfo-loadavg.c>
  $<${is-linux}:src/unix/procfs-exepath.c>
  $<${is-linux}:src/unix/linux-syscalls.c>
  $<${is-linux}:src/unix/linux-inotify.c>
  $<${is-linux}:src/unix/linux-core.c>

  $<${is-darwin}:src/unix/darwin-proctitle.c>
  $<${is-darwin}:src/unix/darwin.c>
  $<${is-darwin}:src/unix/fsevents.c>
 
  $<${is-openbsd}:src/unix/openbsd.c>
  $<${is-netbsd}:src/unix/netbsd.c>

  $<${is-bsd}:src/unix/bsd-ifaddrs.c>
  $<${is-bsd}:src/unix/kqueue.c>

  $<${is-sunos}:src/unix/no-proctitle.c>
  $<${is-sunos}:src/unix/sunos.c>

  $<${is-aix}:src/unix/aix.c>

  $<${is-os/390}:src/unix/pthread-barrier.c>
  $<${is-os/390}:src/unix/pthread-fixes.c>
  $<${is-os/390}:src/unix/os390-syscalls.c>
  $<${is-os/390}:src/unix/os390.c>

  $<${is-android}:src/unix/android-ifaddrs.c>
  $<${is-android}:src/unix/linux-core.c>
  $<${is-android}:src/unix/linux-inotify.c>
  $<${is-android}:src/unix/linux-syscalls.c>
  $<${is-android}:src/unix/procfs-exepath.c>
  $<${is-android}:src/unix/pthread-fixes.c>
  $<${is-android}:src/unix/sysinfo-loadavc.c>
  $<${is-android}:src/unix/sysinfo-memory.c>

  $<${is-windows}:src/win/async.c>
  $<${is-windows}:src/win/core.c>
  $<${is-windows}:src/win/detect-wakeup.c>
  $<${is-windows}:src/win/dl.c>
  $<${is-windows}:src/win/error.c>
  $<${is-windows}:src/win/fs.c>
  $<${is-windows}:src/win/fs-event.c>
  $<${is-windows}:src/win/getaddrinfo.c>
  $<${is-windows}:src/win/getnameinfo.c>
  $<${is-windows}:src/win/handle.c>
  $<${is-windows}:src/win/pipe.c>
  $<${is-windows}:src/win/thread.c>
  $<${is-windows}:src/win/poll.c>
  $<${is-windows}:src/win/process.c>
  $<${is-windows}:src/win/process-stdio.c>
  $<${is-windows}:src/win/signal.c>
  $<${is-windows}:src/win/stream.c>
  $<${is-windows}:src/win/tcp.c>
  $<${is-windows}:src/win/tty.c>
  $<${is-windows}:src/win/udp.c>
  $<${is-windows}:src/win/util.c>
  $<${is-windows}:src/win/winapi.c>
  $<${is-windows}:src/win/winsock.c>
  
  $<${is-unix}:src/unix/async.c>
  $<${is-unix}:src/unix/core.c>
  $<${is-unix}:src/unix/dl.c>
  $<${is-unix}:src/unix/fs.c>
  $<${is-unix}:src/unix/getaddrinfo.c>
  $<${is-unix}:src/unix/getnameinfo.c>
  $<${is-unix}:src/unix/loop.c>
  $<${is-unix}:src/unix/pipe.c>
  $<${is-unix}:src/unix/poll.c>
  $<${is-unix}:src/unix/process.c>
  $<${is-unix}:src/unix/signal.c>
  $<${is-unix}:src/unix/stream.c>
  $<${is-unix}:src/unix/tcp.c>
  $<${is-unix}:src/unix/thread.c>
  $<${is-unix}:src/unix/tty.c>
  $<${is-unix}:src/unix/udp.c>)

add_library(uv-common INTERFACE)

target_include_directories(uv-common
  INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_definitions(uv-common
  INTERFACE
    $<${is-darwin}:_DARWIN_UNLIMITED_SELECT=1>
    $<${is-darwin}:_DARWIN_USE_64_BIT_INODE=1>

    $<${is-windows}:WIN32_LEAN_AND_MEAN>
    $<${is-windows}:_WIN32_WINNT=0x0600>

    $<${is-linux}:_POSIX_C_SOURCE=200112>
    $<${is-linux}:_GNU_SOURCE>

    $<${is-unix}:_FILE_OFFSET_BITS=64>
    $<${is-unix}:_LARGEFILE_SOURCE>

    $<${is-os/390}:_XOPEN_SOURCE_EXTENDED>
    $<${is-os/390}:_OPEN_SYS_SOCK_IPV6>
    $<${is-os/390}:_OPEN_SYS_FILE_EXT>
    $<${is-os/390}:_OPEN_SYS_IF_EXT>
    $<${is-os/390}:_OPEN_MSGQ_EXT>
    $<${is-os/390}:_UNIX03_WITHDRAWN>
    $<${is-os/390}:_UNIX03_THREADS>
    $<${is-os/390}:_UNIX03_SOURCE>
    $<${is-os/390}:_LARGE_TIME_API>
    $<${is-os/390}:_AE_BIMODAL>
    $<${is-os/390}:_ALL_SOURCE>
    $<${is-os/390}:PATH_MAX=255>

    $<${is-sunos}:_XOPEN_SOURCE=500>
    $<${is-sunos}:__EXTENSIONS__>

    $<${is-aix}:_LINUX_SOURCE_COMPAT>
    $<${is-aix}:_XOPEN_SOURCE=500>
    $<${is-aix}:_THREAD_SAFE>
    $<${is-aix}:_ALL_SOURCE>)

target_compile_options(uv-common
  INTERFACE
     $<${is-msvc}:/W4>
     $<$<AND:${is-windows},${is-clang-or-gcc}>:-Wno-unknown-pragmas>
     $<${is-clang-or-gcc}:-Wall -Wextra -Wstrict-prototypes -Wno-unused-parameter>
     $<${is-clang-or-gcc}:--std=gnu89>)

target_link_libraries(uv-common
  INTERFACE
    ${CMAKE_THREAD_LIBS_INIT}
    ${CMAKE_DL_LIBS}

    $<${is-not-darwin-bsd}:kvm>

    $<${is-windows}:advapi32>
    $<${is-windows}:iphlpapi>
    $<${is-windows}:psapi>
    $<${is-windows}:secur32>
    $<${is-windows}:user32>
    $<${is-windows}:userenv>
    $<${is-windows}:ws2_32>

    $<${is-linux}:rt>

    $<${is-sunos}:sendfile>
    $<${is-sunos}:socket>
    $<${is-sunos}:kstat>
    $<${is-sunos}:nsl>

    $<${is-aix}:perfstat>)


add_library(uv_a STATIC ${uv-sources})
add_library(uv SHARED ${uv-sources})

add_library(uv::static ALIAS uv_a)
add_library(uv::shared ALIAS uv)

target_include_directories(uv_a
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)
target_include_directories(uv
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)

target_compile_definitions(uv
  PRIVATE
    BUILDING_UV_SHARED=1)

target_link_libraries(uv_a PRIVATE uv-common)
target_link_libraries(uv PRIVATE uv-common)

if (UV_BUILD_TESTS)
  set(uv-test-sources
      $<${is-windows}:test/runner-win.c>
      $<${is-unix}:test/runner-unix.c>
      test/blackhole-server.c
      test/echo-server.c
      test/run-tests.c
      test/runner.c
      test/test-active.c
      test/test-async-null-cb.c
      test/test-async.c
      test/test-barrier.c
      test/test-buf.c
      test/test-callback-order.c
      test/test-callback-stack.c
      test/test-close-fd.c
      test/test-close-order.c
      test/test-condvar.c
      test/test-connect-unspecified.c
      test/test-connection-fail.c
      test/test-cwd-and-chdir.c
      test/test-default-loop-close.c
      test/test-delayed-accept.c
      test/test-dlerror.c
      test/test-eintr-handling.c
      test/test-embed.c
      test/test-emfile.c
      test/test-env-vars.c
      test/test-error.c
      test/test-fail-always.c
      test/test-fork.c
      test/test-fs-copyfile.c
      test/test-fs-event.c
      test/test-fs-poll.c
      test/test-fs.c
      test/test-fs-readdir.c
      test/test-get-currentexe.c
      test/test-get-loadavg.c
      test/test-get-memory.c
      test/test-get-passwd.c
      test/test-getaddrinfo.c
      test/test-gethostname.c
      test/test-getnameinfo.c
      test/test-getsockname.c
      test/test-getters-setters.c
      test/test-gettimeofday.c
      test/test-handle-fileno.c
      test/test-homedir.c
      test/test-hrtime.c
      test/test-idle.c
      test/test-idna.c
      test/test-ip4-addr.c
      test/test-ip6-addr.c
      test/test-ip6-addr.c
      test/test-ipc-heavy-traffic-deadlock-bug.c
      test/test-ipc-send-recv.c
      test/test-ipc.c
      test/test-loop-alive.c
      test/test-loop-close.c
      test/test-loop-configure.c
      test/test-loop-handles.c
      test/test-loop-stop.c
      test/test-loop-time.c
      test/test-multiple-listen.c
      test/test-mutexes.c
      test/test-osx-select.c
      test/test-pass-always.c
      test/test-ping-pong.c
      test/test-pipe-bind-error.c
      test/test-pipe-close-stdout-read-stdin.c
      test/test-pipe-connect-error.c
      test/test-pipe-connect-multiple.c
      test/test-pipe-connect-prepare.c
      test/test-pipe-getsockname.c
      test/test-pipe-pending-instances.c
      test/test-pipe-sendmsg.c
      test/test-pipe-server-close.c
      test/test-pipe-set-fchmod.c
      test/test-pipe-set-non-blocking.c
      test/test-platform-output.c
      test/test-poll-close-doesnt-corrupt-stack.c
      test/test-poll-close.c
      test/test-poll-closesocket.c
      test/test-poll-oob.c
      test/test-poll.c
      test/test-process-priority.c
      test/test-process-title-threadsafe.c
      test/test-process-title.c
      test/test-queue-foreach-delete.c
      test/test-ref.c
      test/test-run-nowait.c
      test/test-run-once.c
      test/test-semaphore.c
      test/test-shutdown-close.c
      test/test-shutdown-eof.c
      test/test-shutdown-twice.c
      test/test-signal-multiple-loops.c
      test/test-signal.c
      test/test-socket-buffer-size.c
      test/test-spawn.c
      test/test-stdio-over-pipes.c
      test/test-strscpy.c
      test/test-tcp-alloc-cb-fail.c
      test/test-tcp-bind-error.c
      test/test-tcp-bind6-error.c
      test/test-tcp-close-accept.c
      test/test-tcp-close-while-connecting.c
      test/test-tcp-close.c
      test/test-tcp-connect-error-after-write.c
      test/test-tcp-connect-error.c
      test/test-tcp-connect-timeout.c
      test/test-tcp-connect6-error.c
      test/test-tcp-create-socket-early.c
      test/test-tcp-flags.c
      test/test-tcp-oob.c
      test/test-tcp-open.c
      test/test-tcp-read-stop.c
      test/test-tcp-shutdown-after-write.c
      test/test-tcp-try-write.c
      test/test-tcp-unexpected-read.c
      test/test-tcp-write-after-connect.c
      test/test-tcp-write-fail.c
      test/test-tcp-write-queue-order.c
      test/test-tcp-write-to-half-open-connection.c
      test/test-tcp-writealot.c
      test/test-thread-equal.c
      test/test-thread.c
      test/test-thread-affinity.c
      test/test-threadpool-cancel.c
      test/test-threadpool.c
      test/test-timer-again.c
      test/test-timer-from-check.c
      test/test-timer.c
      test/test-tmpdir.c
      test/test-tty-duplicate-key.c
      test/test-tty.c
      test/test-udp-alloc-cb-fail.c
      test/test-udp-bind.c
      test/test-udp-connect.c
      test/test-udp-create-socket-early.c
      test/test-udp-dgram-too-big.c
      test/test-udp-ipv6.c
      test/test-udp-multicast-interface.c
      test/test-udp-multicast-interface6.c
      test/test-udp-multicast-join.c
      test/test-udp-multicast-join6.c
      test/test-udp-multicast-ttl.c
      test/test-udp-open.c
      test/test-udp-options.c
      test/test-udp-send-and-recv.c
      test/test-udp-send-hang-loop.c
      test/test-udp-send-immediate.c
      test/test-udp-send-unreachable.c
      test/test-udp-try-send.c
      test/test-uname.c
      test/test-walk-handles.c
      test/test-watcher-cross-stop.c)

  add_library(uv-tests-common INTERFACE)
  target_link_libraries(uv-tests-common
    INTERFACE
      $<${is-windows}:ws2_32>
      $<${is-linux}:util>
      $<${is-bsd}:util>
      uv-common)

  add_executable(uv-run-tests-shared ${uv-test-sources})
  add_executable(uv-run-tests-static ${uv-test-sources})

  target_compile_definitions(uv-run-tests-shared PRIVATE USING_UV_SHARED=1)

  target_link_libraries(uv-run-tests-shared PRIVATE uv-tests-common uv::shared)
  target_link_libraries(uv-run-tests-static PRIVATE uv-tests-common uv::static)

  add_test(NAME uv::test::static
    COMMAND uv-run-tests-static
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  add_test(NAME uv::test::shared
    COMMAND uv-run-tests-shared
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

if(UNIX)
  # Now for some gibbering horrors from beyond the stars...
  foreach(x ${uv_libraries})
    set(LIBS "${LIBS} -l${x}")
  endforeach(x)
  file(STRINGS configure.ac configure_ac REGEX ^AC_INIT)
  string(REGEX MATCH [0-9]+[.][0-9]+[.][0-9]+ PACKAGE_VERSION "${configure_ac}")
  set(includedir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
  set(libdir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
  set(prefix ${CMAKE_INSTALL_PREFIX})
  configure_file(libuv.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libuv.pc @ONLY)

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libuv.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(TARGETS uv LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(TARGETS uv_a ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
