cmake_minimum_required(VERSION 2.8)
project(libuv C)

option(LIBUV_BUILD_TESTS "builds libuv tests and benchmarks if set to ON" OFF)

set(LIBUVDIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "libuv library" FORCE)

include_directories(${LIBUVDIR}/include ${LIBUVDIR}/src)
set(SOURCES
    ${LIBUVDIR}/include/uv.h
    ${LIBUVDIR}/include/uv/tree.h
    ${LIBUVDIR}/include/uv/errno.h
    ${LIBUVDIR}/include/uv/threadpool.h
    ${LIBUVDIR}/include/uv/version.h
    ${LIBUVDIR}/src/fs-poll.c
    ${LIBUVDIR}/src/heap-inl.h
    ${LIBUVDIR}/src/idna.c
    ${LIBUVDIR}/src/idna.h
    ${LIBUVDIR}/src/inet.c
    ${LIBUVDIR}/src/queue.h
    ${LIBUVDIR}/src/strscpy.c
    ${LIBUVDIR}/src/strscpy.h
    ${LIBUVDIR}/src/threadpool.c
    ${LIBUVDIR}/src/timer.c
    ${LIBUVDIR}/src/uv-common.c
    ${LIBUVDIR}/src/uv-common.h
    ${LIBUVDIR}/src/uv-data-getter-setters.c
    ${LIBUVDIR}/src/version.c
    )

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0600 -D_GNU_SOURCE -D_CRT_SECURE_NO_WARNINGS)
    include_directories(${LIBUVDIR}/src/win)
    set (SOURCES ${SOURCES}
        ${LIBUVDIR}/include/uv/win.h
        ${LIBUVDIR}/src/win/async.c
        ${LIBUVDIR}/src/win/atomicops-inl.h
        ${LIBUVDIR}/src/win/core.c
        ${LIBUVDIR}/src/win/detect-wakeup.c
        ${LIBUVDIR}/src/win/dl.c
        ${LIBUVDIR}/src/win/error.c
        ${LIBUVDIR}/src/win/fs.c
        ${LIBUVDIR}/src/win/fs-event.c
        ${LIBUVDIR}/src/win/getaddrinfo.c
        ${LIBUVDIR}/src/win/getnameinfo.c
        ${LIBUVDIR}/src/win/handle.c
        ${LIBUVDIR}/src/win/handle-inl.h
        ${LIBUVDIR}/src/win/internal.h
        ${LIBUVDIR}/src/win/loop-watcher.c
        ${LIBUVDIR}/src/win/pipe.c
        ${LIBUVDIR}/src/win/poll.c
        ${LIBUVDIR}/src/win/process.c
        ${LIBUVDIR}/src/win/process-stdio.c
        ${LIBUVDIR}/src/win/req-inl.h
        ${LIBUVDIR}/src/win/signal.c
        ${LIBUVDIR}/src/win/snprintf.c
        ${LIBUVDIR}/src/win/stream.c
        ${LIBUVDIR}/src/win/stream-inl.h
        ${LIBUVDIR}/src/win/tcp.c
        ${LIBUVDIR}/src/win/thread.c
        ${LIBUVDIR}/src/win/tty.c
        ${LIBUVDIR}/src/win/udp.c
        ${LIBUVDIR}/src/win/util.c
        ${LIBUVDIR}/src/win/winapi.c
        ${LIBUVDIR}/src/win/winapi.h
        ${LIBUVDIR}/src/win/winsock.c
        ${LIBUVDIR}/src/win/winsock.h
        )

    add_library(uv ${SOURCES})
    target_link_libraries(uv advapi32 iphlpapi psapi userenv shell32 ws2_32)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g --std=gnu89 -pedantic -Wall -Wextra -Wno-unused-parameter")
    add_definitions(-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64)
    include_directories(${LIBUVDIR}/src/unix)
    set(SOURCES ${SOURCES}
        ${LIBUVDIR}/include/uv/unix.h
        ${LIBUVDIR}/include/uv/linux.h
        ${LIBUVDIR}/include/uv/sunos.h
        ${LIBUVDIR}/include/uv/darwin.h
        ${LIBUVDIR}/include/uv/bsd.h
        ${LIBUVDIR}/include/uv/aix.h
        # ${LIBUVDIR}/src/unix/android-ifaddrs.c
        ${LIBUVDIR}/src/unix/async.c
        ${LIBUVDIR}/src/unix/atomic-ops.h
        # ${LIBUVDIR}/src/unix/bsd-ifaddrs.c
        # ${LIBUVDIR}/src/unix/bsd-proctitle.c
        ${LIBUVDIR}/src/unix/core.c
        # ${LIBUVDIR}/src/unix/cygwin.c
        ${LIBUVDIR}/src/unix/dl.c
        # ${LIBUVDIR}/src/unix/freebsd.c
        ${LIBUVDIR}/src/unix/fs.c
        # ${LIBUVDIR}/src/unix/fsevents.c
        ${LIBUVDIR}/src/unix/getaddrinfo.c
        ${LIBUVDIR}/src/unix/getnameinfo.c
        # ${LIBUVDIR}/src/unix/ibmi.c
        ${LIBUVDIR}/src/unix/internal.h
        ${LIBUVDIR}/src/unix/loop.c
        ${LIBUVDIR}/src/unix/loop-watcher.c
        # ${LIBUVDIR}/src/unix/netbsd.c
        # ${LIBUVDIR}/src/unix/no-fsevents.c
        ${LIBUVDIR}/src/unix/no-proctitle.c
        # ${LIBUVDIR}/src/unix/openbsd.c
        # ${LIBUVDIR}/src/unix/os390-syscalls.c
        # ${LIBUVDIR}/src/unix/os390-syscalls.h
        # ${LIBUVDIR}/src/unix/os390.c
        ${LIBUVDIR}/src/unix/pipe.c
        ${LIBUVDIR}/src/unix/poll.c
        # ${LIBUVDIR}/src/unix/posix-hrtime.c
        # ${LIBUVDIR}/src/unix/posix-poll.c
        ${LIBUVDIR}/src/unix/process.c
        ${LIBUVDIR}/src/unix/procfs-exepath.c
        ${LIBUVDIR}/src/unix/proctitle.c
        ${LIBUVDIR}/src/unix/pthread-fixes.c
        ${LIBUVDIR}/src/unix/signal.c
        ${LIBUVDIR}/src/unix/spinlock.h
        ${LIBUVDIR}/src/unix/stream.c
        # ${LIBUVDIR}/src/unix/sysinfo-loadavg.c
        # ${LIBUVDIR}/src/unix/sysinfo-memory.c
        ${LIBUVDIR}/src/unix/tcp.c
        ${LIBUVDIR}/src/unix/thread.c
        ${LIBUVDIR}/src/unix/tty.c
        ${LIBUVDIR}/src/unix/udp.c
        )

    if (APPLE)
        add_definitions(-D_DARWIN_USE_64_BIT_INODE=1 -D_DARWIN_UNLIMITED_SELECT=1)
        set(SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/kqueue.c
            ${LIBUVDIR}/src/unix/darwin.c
            ${LIBUVDIR}/src/unix/fsevents.c
            ${LIBUVDIR}/src/unix/darwin-proctitle.c
            )

        #This is necessary to mute harmless warnings, as documented by https://github.com/libuv/libuv/issues/454
        set_source_files_properties(${LIBUVDIR}/src/unix/stream.c PROPERTIES COMPILE_FLAGS -Wno-gnu-folding-constant)
    endif()

    if (NOT APPLE)
        add_definitions(-Wstrict-aliasing)
    endif()

    if ((${CMAKE_SYSTEM_NAME} MATCHES "Linux") OR (${CMAKE_SYSTEM_NAME} MATCHES "Android"))
        add_definitions(-D_GNU_SOURCE -D_POSIX_C_SOURCE=200112)
        set(SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/linux-core.c
            ${LIBUVDIR}/src/unix/linux-inotify.c
            ${LIBUVDIR}/src/unix/linux-syscalls.c
            ${LIBUVDIR}/src/unix/linux-syscalls.h
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
        add_definitions(-D__EXTENSIONS__ -D_XOPEN_SOURCE=500)
        set(SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/sunos.c
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "AIX")
        add_definitions(-D_ALL_SOURCE -D_XOPEN_SOURCE=500 -D_LINUX_SOURCE_COMPAT)
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/aic.c
            ${LIBUVDIR}/src/unix/aix-common.c
            )
        find_library(PERFSTAT_LIB NAMES perfstat)
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "DragonFlyBSD")
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/freebsd.c
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/openbsd.c
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/netbsd.c
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "DragonFlyBSD" OR
            ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/kqueue.c
            )
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "Android")
        set (SOURCES ${SOURCES}
            ${LIBUVDIR}/include/android-ifaddrs.h
            ${LIBUVDIR}/include/pthread-barrier.h
            ${LIBUVDIR}/src/unix/android-ifaddrs.c
            )
    endif()

    add_library(uv ${SOURCES})

    find_library(M_LIB NAMES m)
    find_package(Threads)
    target_link_libraries(uv ${CMAKE_THREAD_LIBS_INIT} ${M_LIB})

    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        find_library(DL_LIB NAMES dl)
        find_library(RT_LIB NAMES rt)
        target_link_libraries(uv ${DL_LIB} ${RT_LIB})
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
        find_library(KSTAT_LIB NAMES kstat)
        find_library(NSL_LIB NAMES nsl)
        find_library(SENDFILE_LIB NAMES sendfile)
        find_library(SOCKET_LIB NAMES socket)
        target_link_libraries(uv ${KSTAT_LIB} ${NSL_LIB} ${SENDFILE_LIB} ${SOCKET_LIB})
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "AIX")
        find_library(PERFSTAT_LIB NAMES perfstat)
        target_link_libraries(uv ${PERFSTAT_LIB})
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "DragonFlyBSD" OR
            ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
        find_library(KVM_LIB NAMES kvm)
        target_link_libraries(uv ${KVM_LIB})
    endif()

endif()

if(LIBUV_BUILD_TESTS)
    set (TEST_SOURCES
        ${LIBUVDIR}/test/blackhole-server.c
        ${LIBUVDIR}/test/echo-server.c
        ${LIBUVDIR}/test/run-tests.c
        ${LIBUVDIR}/test/runner.c
        ${LIBUVDIR}/test/runner.h
        ${LIBUVDIR}/test/task.h
        ${LIBUVDIR}/test/test-active.c
        ${LIBUVDIR}/test/test-async-null-cb.c
        ${LIBUVDIR}/test/test-async.c
        ${LIBUVDIR}/test/test-barrier.c
        ${LIBUVDIR}/test/test-callback-order.c
        ${LIBUVDIR}/test/test-callback-stack.c
        ${LIBUVDIR}/test/test-close-fd.c
        ${LIBUVDIR}/test/test-close-order.c
        ${LIBUVDIR}/test/test-condvar.c
        ${LIBUVDIR}/test/test-connection-fail.c
        ${LIBUVDIR}/test/test-cwd-and-chdir.c
        ${LIBUVDIR}/test/test-default-loop-close.c
        ${LIBUVDIR}/test/test-delayed-accept.c
        ${LIBUVDIR}/test/test-dlerror.c
        ${LIBUVDIR}/test/test-eintr-handling.c
        ${LIBUVDIR}/test/test-embed.c
        ${LIBUVDIR}/test/test-emfile.c
        ${LIBUVDIR}/test/test-error.c
        ${LIBUVDIR}/test/test-fail-always.c
        ${LIBUVDIR}/test/test-fs-event.c
        ${LIBUVDIR}/test/test-fs-poll.c
        ${LIBUVDIR}/test/test-fs.c
        ${LIBUVDIR}/test/test-get-currentexe.c
        ${LIBUVDIR}/test/test-get-loadavg.c
        ${LIBUVDIR}/test/test-get-loadavg.c
        ${LIBUVDIR}/test/test-get-memory.c
        ${LIBUVDIR}/test/test-get-passwd.c
        ${LIBUVDIR}/test/test-getaddrinfo.c
        ${LIBUVDIR}/test/test-getnameinfo.c
        ${LIBUVDIR}/test/test-getsockname.c
        ${LIBUVDIR}/test/test-handle-fileno.c
        ${LIBUVDIR}/test/test-homedir.c
        ${LIBUVDIR}/test/test-hrtime.c
        ${LIBUVDIR}/test/test-idle.c
        ${LIBUVDIR}/test/test-ip4-addr.c
        ${LIBUVDIR}/test/test-ip6-addr.c
        ${LIBUVDIR}/test/test-ipc-send-recv.c
        ${LIBUVDIR}/test/test-ipc.c
        ${LIBUVDIR}/test/test-list.h
        ${LIBUVDIR}/test/test-loop-alive.c
        ${LIBUVDIR}/test/test-loop-close.c
        ${LIBUVDIR}/test/test-loop-configure.c
        ${LIBUVDIR}/test/test-loop-handles.c
        ${LIBUVDIR}/test/test-loop-stop.c
        ${LIBUVDIR}/test/test-loop-time.c
        ${LIBUVDIR}/test/test-multiple-listen.c
        ${LIBUVDIR}/test/test-mutexes.c
        ${LIBUVDIR}/test/test-osx-select.c
        ${LIBUVDIR}/test/test-pass-always.c
        ${LIBUVDIR}/test/test-ping-pong.c
        ${LIBUVDIR}/test/test-pipe-bind-error.c
        ${LIBUVDIR}/test/test-pipe-close-stdout-read-stdin.c
        ${LIBUVDIR}/test/test-pipe-connect-error.c
        ${LIBUVDIR}/test/test-pipe-connect-multiple.c
        ${LIBUVDIR}/test/test-pipe-connect-prepare.c
        ${LIBUVDIR}/test/test-pipe-getsockname.c
        ${LIBUVDIR}/test/test-pipe-pending-instances.c
        ${LIBUVDIR}/test/test-pipe-sendmsg.c
        ${LIBUVDIR}/test/test-pipe-server-close.c
        ${LIBUVDIR}/test/test-pipe-set-non-blocking.c
        ${LIBUVDIR}/test/test-platform-output.c
        ${LIBUVDIR}/test/test-poll-close.c
        ${LIBUVDIR}/test/test-poll.c
        ${LIBUVDIR}/test/test-process-title.c
        ${LIBUVDIR}/test/test-queue-foreach-delete.c
        ${LIBUVDIR}/test/test-ref.c
        ${LIBUVDIR}/test/test-run-nowait.c
        ${LIBUVDIR}/test/test-run-once.c
        ${LIBUVDIR}/test/test-semaphore.c
        ${LIBUVDIR}/test/test-shutdown-close.c
        ${LIBUVDIR}/test/test-shutdown-eof.c
        ${LIBUVDIR}/test/test-shutdown-twice.c
        ${LIBUVDIR}/test/test-signal-multiple-loops.c
        ${LIBUVDIR}/test/test-signal.c
        ${LIBUVDIR}/test/test-socket-buffer-size.c
        ${LIBUVDIR}/test/test-spawn.c
        ${LIBUVDIR}/test/test-stdio-over-pipes.c
        ${LIBUVDIR}/test/test-tcp-bind-error.c
        ${LIBUVDIR}/test/test-tcp-bind6-error.c
        ${LIBUVDIR}/test/test-tcp-close-accept.c
        ${LIBUVDIR}/test/test-tcp-close-while-connecting.c
        ${LIBUVDIR}/test/test-tcp-close.c
        ${LIBUVDIR}/test/test-tcp-connect-error-after-write.c
        ${LIBUVDIR}/test/test-tcp-connect-error.c
        ${LIBUVDIR}/test/test-tcp-connect-timeout.c
        ${LIBUVDIR}/test/test-tcp-connect6-error.c
        ${LIBUVDIR}/test/test-tcp-create-socket-early.c
        ${LIBUVDIR}/test/test-tcp-flags.c
        ${LIBUVDIR}/test/test-tcp-oob.c
        ${LIBUVDIR}/test/test-tcp-open.c
        ${LIBUVDIR}/test/test-tcp-read-stop.c
        ${LIBUVDIR}/test/test-tcp-shutdown-after-write.c
        ${LIBUVDIR}/test/test-tcp-try-write.c
        ${LIBUVDIR}/test/test-tcp-unexpected-read.c
        ${LIBUVDIR}/test/test-tcp-write-after-connect.c
        ${LIBUVDIR}/test/test-tcp-write-fail.c
        ${LIBUVDIR}/test/test-tcp-write-queue-order.c
        ${LIBUVDIR}/test/test-tcp-write-to-half-open-connection.c
        ${LIBUVDIR}/test/test-tcp-writealot.c
        ${LIBUVDIR}/test/test-thread-equal.c
        ${LIBUVDIR}/test/test-thread.c
        ${LIBUVDIR}/test/test-threadpool-cancel.c
        ${LIBUVDIR}/test/test-threadpool.c
        ${LIBUVDIR}/test/test-timer-again.c
        ${LIBUVDIR}/test/test-timer-from-check.c
        ${LIBUVDIR}/test/test-timer.c
        ${LIBUVDIR}/test/test-tmpdir.c
        ${LIBUVDIR}/test/test-tty.c
        ${LIBUVDIR}/test/test-udp-bind.c
        ${LIBUVDIR}/test/test-udp-create-socket-early.c
        ${LIBUVDIR}/test/test-udp-dgram-too-big.c
        ${LIBUVDIR}/test/test-udp-ipv6.c
        ${LIBUVDIR}/test/test-udp-multicast-interface.c
        ${LIBUVDIR}/test/test-udp-multicast-interface6.c
        ${LIBUVDIR}/test/test-udp-multicast-join.c
        ${LIBUVDIR}/test/test-udp-multicast-join6.c
        ${LIBUVDIR}/test/test-udp-multicast-ttl.c
        ${LIBUVDIR}/test/test-udp-open.c
        ${LIBUVDIR}/test/test-udp-options.c
        ${LIBUVDIR}/test/test-udp-send-and-recv.c
        ${LIBUVDIR}/test/test-udp-send-immediate.c
        ${LIBUVDIR}/test/test-udp-send-unreachable.c
        ${LIBUVDIR}/test/test-udp-try-send.c
        ${LIBUVDIR}/test/test-walk-handles.c
        ${LIBUVDIR}/test/test-watcher-cross-stop.c
        )

    if(WIN32)
        set (TEST_SOURCES ${TEST_SOURCES}
            #Windows-only tests
            ${LIBUVDIR}/test/test-poll-close-doesnt-corrupt-stack.c
            ${LIBUVDIR}/test/test-poll-closesocket.c

            #Windows Runner
            ${LIBUVDIR}/test/runner-win.c
            ${LIBUVDIR}/test/runner-win.h
            )
    else()
        set (TEST_SOURCES ${TEST_SOURCES}
            #Unix runner
            ${LIBUVDIR}/test/runner-unix.c
            ${LIBUVDIR}/test/runner-unix.h
            )
    endif()

    file(COPY ${LIBUVDIR}/test/fixtures DESTINATION test)

    add_executable(run-tests ${TEST_SOURCES})
    set_property(TARGET run-tests PROPERTY C_STANDARD 99)
    target_link_libraries(run-tests uv)

    set(BENCHMARK_SOURCES
        ${LIBUVDIR}/test/blackhole-server.c
        ${LIBUVDIR}/test/dns-server.c
        ${LIBUVDIR}/test/echo-server.c
        ${LIBUVDIR}/test/run-benchmarks.c
        ${LIBUVDIR}/test/runner.c
        ${LIBUVDIR}/test/runner.h
        ${LIBUVDIR}/test/task.h
        ${LIBUVDIR}/test/benchmark-async-pummel.c
        ${LIBUVDIR}/test/benchmark-async.c
        ${LIBUVDIR}/test/benchmark-fs-stat.c
        ${LIBUVDIR}/test/benchmark-getaddrinfo.c
        ${LIBUVDIR}/test/benchmark-list.h
        ${LIBUVDIR}/test/benchmark-loop-count.c
        ${LIBUVDIR}/test/benchmark-million-async.c
        ${LIBUVDIR}/test/benchmark-million-timers.c
        ${LIBUVDIR}/test/benchmark-multi-accept.c
        ${LIBUVDIR}/test/benchmark-ping-pongs.c
        ${LIBUVDIR}/test/benchmark-pound.c
        ${LIBUVDIR}/test/benchmark-pump.c
        ${LIBUVDIR}/test/benchmark-sizes.c
        ${LIBUVDIR}/test/benchmark-spawn.c
        ${LIBUVDIR}/test/benchmark-tcp-write-batch.c
        ${LIBUVDIR}/test/benchmark-thread.c
        ${LIBUVDIR}/test/benchmark-udp-pummel.c
        )

    if(WIN32)
        set (BENCHMARK_SOURCES ${BENCHMARK_SOURCES}
            ${LIBUVDIR}/test/runner-win.c
            ${LIBUVDIR}/test/runner-win.h
            )
    else()
        set (BENCHMARK_SOURCES ${BENCHMARK_SOURCES}
            ${LIBUVDIR}/test/runner-unix.c
            ${LIBUVDIR}/test/runner-unix.h
            )
    endif()

    add_executable(run-benchmarks ${BENCHMARK_SOURCES})
    target_link_libraries(run-benchmarks uv)
endif()

#[[

# TODO: determine CMAKE_SYSTEM_NAME on OS/390.  Currently assumes "OS/390".
cmake_minimum_required(VERSION 3.0)
project(libuv)
enable_testing()

if(MSVC)
  list(APPEND uv_cflags /W4)
elseif(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  list(APPEND uv_cflags -fvisibility=hidden --std=gnu89)
  list(APPEND uv_cflags -Wall -Wextra -Wstrict-prototypes)
  list(APPEND uv_cflags -Wno-unused-parameter)
endif()

set(uv_sources
    src/fs-poll.c
    src/idna.c
    src/inet.c
    src/strscpy.c
    src/threadpool.c
    src/timer.c
    src/uv-common.c
    src/uv-data-getter-setters.c
    src/version.c)

set(uv_test_sources
    test/blackhole-server.c
    test/echo-server.c
    test/run-tests.c
    test/runner.c
    test/test-active.c
    test/test-async-null-cb.c
    test/test-async.c
    test/test-barrier.c
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

if(WIN32)
  list(APPEND uv_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
  list(APPEND uv_libraries
       advapi32
       iphlpapi
       psapi
       shell32
       user32
       userenv
       ws2_32)
  list(APPEND uv_sources
       src/win/async.c
       src/win/core.c
       src/win/detect-wakeup.c
       src/win/dl.c
       src/win/error.c
       src/win/fs.c
       src/win/fs-event.c
       src/win/getaddrinfo.c
       src/win/getnameinfo.c
       src/win/handle.c
       src/win/loop-watcher.c
       src/win/pipe.c
       src/win/thread.c
       src/win/poll.c
       src/win/process.c
       src/win/process-stdio.c
       src/win/signal.c
       src/win/snprintf.c
       src/win/stream.c
       src/win/tcp.c
       src/win/tty.c
       src/win/udp.c
       src/win/util.c
       src/win/winapi.c
       src/win/winsock.c)
  list(APPEND uv_test_libraries ws2_32)
  list(APPEND uv_test_sources src/win/snprintf.c test/runner-win.c)
else()
  list(APPEND uv_defines _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android has pthread as part of its c library, not as a separate
    # libpthread.so.
    list(APPEND uv_libraries pthread)
  endif()
  list(APPEND uv_sources
       src/unix/async.c
       src/unix/core.c
       src/unix/dl.c
       src/unix/fs.c
       src/unix/getaddrinfo.c
       src/unix/getnameinfo.c
       src/unix/loop-watcher.c
       src/unix/loop.c
       src/unix/pipe.c
       src/unix/poll.c
       src/unix/process.c
       src/unix/signal.c
       src/unix/stream.c
       src/unix/tcp.c
       src/unix/thread.c
       src/unix/tty.c
       src/unix/udp.c)
  list(APPEND uv_test_sources test/runner-unix.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "AIX")
  list(APPEND uv_defines
       _ALL_SOURCE
       _LINUX_SOURCE_COMPAT
       _THREAD_SAFE
       _XOPEN_SOURCE=500)
  list(APPEND uv_libraries perfstat)
  list(APPEND uv_sources src/unix/aix.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  list(APPEND uv_libs dl)
  list(APPEND uv_sources
       src/unix/android-ifaddrs.c
       src/unix/linux-core.c
       src/unix/linux-inotify.c
       src/unix/linux-syscalls.c
       src/unix/procfs-exepath.c
       src/unix/pthread-fixes.c
       src/unix/sysinfo-loadavg.c
       src/unix/sysinfo-memory.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Android|Darwin|Linux|OS/390")
  list(APPEND uv_sources src/unix/proctitle.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD")
  list(APPEND uv_sources src/unix/freebsd.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|NetBSD|OpenBSD")
  list(APPEND uv_sources src/unix/posix-hrtime.c src/unix/bsd-proctitle.c)
  list(APPEND uv_libraries kvm)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin|DragonFly|FreeBSD|NetBSD|OpenBSD")
  list(APPEND uv_sources src/unix/bsd-ifaddrs.c src/unix/kqueue.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND uv_defines _DARWIN_UNLIMITED_SELECT=1 _DARWIN_USE_64_BIT_INODE=1)
  list(APPEND uv_sources
       src/unix/darwin-proctitle.c
       src/unix/darwin.c
       src/unix/fsevents.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND uv_defines _GNU_SOURCE _POSIX_C_SOURCE=200112)
  list(APPEND uv_libraries dl rt)
  list(APPEND uv_sources
       src/unix/linux-core.c
       src/unix/linux-inotify.c
       src/unix/linux-syscalls.c
       src/unix/procfs-exepath.c
       src/unix/sysinfo-loadavg.c
       src/unix/sysinfo-memory.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
  list(APPEND uv_sources src/unix/netbsd.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  list(APPEND uv_sources src/unix/openbsd.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OS/390")
  list(APPEND uv_defines PATH_MAX=255)
  list(APPEND uv_defines _AE_BIMODAL)
  list(APPEND uv_defines _ALL_SOURCE)
  list(APPEND uv_defines _LARGE_TIME_API)
  list(APPEND uv_defines _OPEN_MSGQ_EXT)
  list(APPEND uv_defines _OPEN_SYS_FILE_EXT)
  list(APPEND uv_defines _OPEN_SYS_IF_EXT)
  list(APPEND uv_defines _OPEN_SYS_SOCK_IPV6)
  list(APPEND uv_defines _UNIX03_SOURCE)
  list(APPEND uv_defines _UNIX03_THREADS)
  list(APPEND uv_defines _UNIX03_WITHDRAWN)
  list(APPEND uv_defines _XOPEN_SOURCE_EXTENDED)
  list(APPEND uv_sources
       src/unix/pthread-fixes.c
       src/unix/pthread-barrier.c
       src/unix/os390.c
       src/unix/os390-syscalls.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
  list(APPEND uv_defines __EXTENSIONS__ _XOPEN_SOURCE=500)
  list(APPEND uv_libraries kstat nsl sendfile socket)
  list(APPEND uv_sources src/unix/no-proctitle.c src/unix/sunos.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin|DragonFly|FreeBSD|Linux|NetBSD|OpenBSD")
  list(APPEND uv_test_libraries util)
endif()

add_library(uv SHARED ${uv_sources})
target_compile_definitions(uv PRIVATE ${uv_defines} BUILDING_UV_SHARED=1)
target_compile_options(uv PRIVATE ${uv_cflags})
target_include_directories(uv PRIVATE include src)
target_link_libraries(uv ${uv_libraries})

add_library(uv_a STATIC ${uv_sources})
target_compile_definitions(uv_a PRIVATE ${uv_defines})
target_compile_options(uv_a PRIVATE ${uv_cflags})
target_include_directories(uv_a PRIVATE include src)
target_link_libraries(uv_a ${uv_libraries})

if(BUILD_TESTING)
  include(CTest)
  add_executable(uv_run_tests ${uv_test_sources})
  target_compile_definitions(uv_run_tests
                             PRIVATE ${uv_defines} USING_UV_SHARED=1)
  target_compile_options(uv_run_tests PRIVATE ${uv_cflags})
  target_include_directories(uv_run_tests PRIVATE include)
  target_link_libraries(uv_run_tests uv ${uv_test_libraries})
  add_test(NAME uv_test
           COMMAND uv_run_tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(uv_run_tests_a ${uv_test_sources})
  target_compile_definitions(uv_run_tests_a PRIVATE ${uv_defines})
  target_compile_options(uv_run_tests_a PRIVATE ${uv_cflags})
  target_include_directories(uv_run_tests_a PRIVATE include)
  target_link_libraries(uv_run_tests_a uv_a ${uv_test_libraries})
  add_test(NAME uv_test_a
           COMMAND uv_run_tests_a
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(UNIX)
  # Now for some gibbering horrors from beyond the stars...
  include(GNUInstallDirs)
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

]]
