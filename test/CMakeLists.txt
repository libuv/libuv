add_library(uv::tests INTERFACE IMPORTED)
add_executable(uv_run_tests_a)
add_executable(uv_run_tests)

string(CONCAT uv-test-library $<
  $<OR:
    $<PLATFORM_ID:DragonFly>,
    $<PLATFORM_ID:FreeBSD>,
    $<PLATFORM_ID:OpenBSD>,
    $<PLATFORM_ID:NetBSD>,
    $<PLATFORM_ID:Darwin>,
    $<PLATFORM_ID:Linux>
  >:util
>)

target_compile_definitions(uv::tests INTERFACE ${uv_defines})
target_compile_options(uv::tests INTERFACE ${uv_cflags})
target_link_libraries(uv::tests
  INTERFACE
    $<$<PLATFORM_ID:Windows>:ws2_32>
    ${uv-test-library})

target_compile_definitions(uv_run_tests PRIVATE USING_UV_SHARED=1)

target_link_libraries(uv_run_tests_a PRIVATE uv::tests uv_a)
target_link_libraries(uv_run_tests PRIVATE uv::tests uv)

add_test(
  NAME uv_test
  COMMAND uv_run_tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(
  NAME uv_test_a
  COMMAND uv_run_tests_a
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

list(APPEND uv_test_sources
    $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/src/win/snprintf.c>
    $<$<PLATFORM_ID:Windows>:runner-win.c>
    blackhole-server.c
    echo-server.c
    run-tests.c
    runner.c
    test-active.c
    test-async-null-cb.c
    test-async.c
    test-barrier.c
    test-callback-order.c
    test-callback-stack.c
    test-close-fd.c
    test-close-order.c
    test-condvar.c
    test-connect-unspecified.c
    test-connection-fail.c
    test-cwd-and-chdir.c
    test-default-loop-close.c
    test-delayed-accept.c
    test-dlerror.c
    test-eintr-handling.c
    test-embed.c
    test-emfile.c
    test-env-vars.c
    test-error.c
    test-fail-always.c
    test-fork.c
    test-fs-copyfile.c
    test-fs-event.c
    test-fs-poll.c
    test-fs.c
    test-fs-readdir.c
    test-fs-fd-hash.c
    test-fs-open-flags.c
    test-get-currentexe.c
    test-get-loadavg.c
    test-get-memory.c
    test-get-passwd.c
    test-getaddrinfo.c
    test-gethostname.c
    test-getnameinfo.c
    test-getsockname.c
    test-getters-setters.c
    test-gettimeofday.c
    test-handle-fileno.c
    test-homedir.c
    test-hrtime.c
    test-idle.c
    test-idna.c
    test-ip4-addr.c
    test-ip6-addr.c
    test-ipc-heavy-traffic-deadlock-bug.c
    test-ipc-send-recv.c
    test-ipc.c
    test-loop-alive.c
    test-loop-close.c
    test-loop-configure.c
    test-loop-handles.c
    test-loop-stop.c
    test-loop-time.c
    test-multiple-listen.c
    test-mutexes.c
    test-osx-select.c
    test-pass-always.c
    test-ping-pong.c
    test-pipe-bind-error.c
    test-pipe-close-stdout-read-stdin.c
    test-pipe-connect-error.c
    test-pipe-connect-multiple.c
    test-pipe-connect-prepare.c
    test-pipe-getsockname.c
    test-pipe-pending-instances.c
    test-pipe-sendmsg.c
    test-pipe-server-close.c
    test-pipe-set-fchmod.c
    test-pipe-set-non-blocking.c
    test-platform-output.c
    test-poll-close-doesnt-corrupt-stack.c
    test-poll-close.c
    test-poll-closesocket.c
    test-poll-oob.c
    test-poll.c
    test-process-priority.c
    test-process-title-threadsafe.c
    test-process-title.c
    test-queue-foreach-delete.c
    test-random.c
    test-ref.c
    test-run-nowait.c
    test-run-once.c
    test-semaphore.c
    test-shutdown-close.c
    test-shutdown-eof.c
    test-shutdown-twice.c
    test-signal-multiple-loops.c
    test-signal-pending-on-close.c
    test-signal.c
    test-socket-buffer-size.c
    test-spawn.c
    test-stdio-over-pipes.c
    test-strscpy.c
    test-tcp-alloc-cb-fail.c
    test-tcp-bind-error.c
    test-tcp-bind6-error.c
    test-tcp-close-accept.c
    test-tcp-close-while-connecting.c
    test-tcp-close.c
    test-tcp-close-reset.c
    test-tcp-connect-error-after-write.c
    test-tcp-connect-error.c
    test-tcp-connect-timeout.c
    test-tcp-connect6-error.c
    test-tcp-create-socket-early.c
    test-tcp-flags.c
    test-tcp-oob.c
    test-tcp-open.c
    test-tcp-read-stop.c
    test-tcp-shutdown-after-write.c
    test-tcp-try-write.c
    test-tcp-try-write-error.c
    test-tcp-unexpected-read.c
    test-tcp-write-after-connect.c
    test-tcp-write-fail.c
    test-tcp-write-queue-order.c
    test-tcp-write-to-half-open-connection.c
    test-tcp-writealot.c
    test-thread-equal.c
    test-thread.c
    test-threadpool-cancel.c
    test-threadpool.c
    test-timer-again.c
    test-timer-from-check.c
    test-timer.c
    test-tmpdir.c
    test-tty-duplicate-key.c
    test-tty.c
    test-udp-alloc-cb-fail.c
    test-udp-bind.c
    test-udp-connect.c
    test-udp-create-socket-early.c
    test-udp-dgram-too-big.c
    test-udp-ipv6.c
    test-udp-multicast-interface.c
    test-udp-multicast-interface6.c
    test-udp-multicast-join.c
    test-udp-multicast-join6.c
    test-udp-multicast-ttl.c
    test-udp-open.c
    test-udp-options.c
    test-udp-send-and-recv.c
    test-udp-send-hang-loop.c
    test-udp-send-immediate.c
    test-udp-send-unreachable.c
    test-udp-try-send.c
    test-uname.c
    test-walk-handles.c
    test-watcher-cross-stop.c)

target_sources(uv_run_tests_a PRIVATE ${uv_test_sources})
target_sources(uv_run_tests PRIVATE ${uv_test_sources})
