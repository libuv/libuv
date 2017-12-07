cmake_minimum_required(VERSION 2.8)
project(libuv C)

option(LIBUV_BUILD_TESTS "builds libuv tests and benchmarks if set to ON" OFF)

set(LIBUVDIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "libuv library" FORCE)

include_directories(${LIBUVDIR}/include ${LIBUVDIR}/src)
set(SOURCES
    ${LIBUVDIR}/include/uv.h
    ${LIBUVDIR}/include/tree.h
    ${LIBUVDIR}/include/uv-errno.h
    ${LIBUVDIR}/include/uv-threadpool.h
    ${LIBUVDIR}/include/uv-version.h
    ${LIBUVDIR}/src/fs-poll.c
    ${LIBUVDIR}/src/heap-inl.h
    ${LIBUVDIR}/src/inet.c
    ${LIBUVDIR}/src/queue.h
    ${LIBUVDIR}/src/threadpool.c
    ${LIBUVDIR}/src/uv-common.c
    ${LIBUVDIR}/src/uv-common.h
    ${LIBUVDIR}/src/version.c
    )

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0600 -D_GNU_SOURCE -D_CRT_SECURE_NO_WARNINGS)
    include_directories(${LIBUVDIR}/src/win)
    set (SOURCES ${SOURCES}
        ${LIBUVDIR}/include/uv-win.h
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
        ${LIBUVDIR}/src/win/thread.c
        ${LIBUVDIR}/src/win/poll.c
        ${LIBUVDIR}/src/win/process.c
        ${LIBUVDIR}/src/win/process-stdio.c
        ${LIBUVDIR}/src/win/req.c
        ${LIBUVDIR}/src/win/req-inl.h
        ${LIBUVDIR}/src/win/signal.c
        ${LIBUVDIR}/src/win/snprintf.c
        ${LIBUVDIR}/src/win/stream.c
        ${LIBUVDIR}/src/win/stream-inl.h
        ${LIBUVDIR}/src/win/tcp.c
        ${LIBUVDIR}/src/win/tty.c
        ${LIBUVDIR}/src/win/timer.c
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
        ${LIBUVDIR}/include/uv-unix.h
        ${LIBUVDIR}/include/uv-linux.h
        ${LIBUVDIR}/include/uv-sunos.h
        ${LIBUVDIR}/include/uv-darwin.h
        ${LIBUVDIR}/include/uv-bsd.h
        ${LIBUVDIR}/include/uv-aix.h
        ${LIBUVDIR}/src/unix/async.c
        ${LIBUVDIR}/src/unix/atomic-ops.h
        ${LIBUVDIR}/src/unix/core.c
        ${LIBUVDIR}/src/unix/dl.c
        ${LIBUVDIR}/src/unix/fs.c
        ${LIBUVDIR}/src/unix/getaddrinfo.c
        ${LIBUVDIR}/src/unix/getnameinfo.c
        ${LIBUVDIR}/src/unix/internal.h
        ${LIBUVDIR}/src/unix/loop.c
        ${LIBUVDIR}/src/unix/loop-watcher.c
        ${LIBUVDIR}/src/unix/pipe.c
        ${LIBUVDIR}/src/unix/poll.c
        ${LIBUVDIR}/src/unix/process.c
        ${LIBUVDIR}/src/unix/signal.c
        ${LIBUVDIR}/src/unix/spinlock.h
        ${LIBUVDIR}/src/unix/stream.c
        ${LIBUVDIR}/src/unix/tcp.c
        ${LIBUVDIR}/src/unix/thread.c
        ${LIBUVDIR}/src/unix/timer.c
        ${LIBUVDIR}/src/unix/tty.c
        ${LIBUVDIR}/src/unix/udp.c
        )

    if (APPLE)
        add_definitions(-D_DARWIN_USE_64_BIT_INODE=1 -D_DARWIN_UNLIMITED_SELECT=1)
        set(SOURCES ${SOURCES}
            ${LIBUVDIR}/src/unix/proctitle.c
            ${LIBUVDIR}/src/unix/kqueue.c
            ${LIBUVDIR}/src/unix/darwin.c
            ${LIBUVDIR}/src/unix/fsevents.c
            ${LIBUVDIR}/src/unix/pthread-barrier.c
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
            ${LIBUVDIR}/src/unix/proctitle.c
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
            ${LIBUVDIR}/src/unix/pthread-barrier.c
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
