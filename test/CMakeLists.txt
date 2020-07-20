# Small hack: use ${uv_test_sources} now to get the runner skeleton,
# before the actual tests are added.
# However, this won't be an issue once we start to use target_sources, and
# create a common test library target
add_executable(uv_run_benchmarks_a
  ${uv_test_sources}
  benchmark-async-pummel.c
  benchmark-async.c
  benchmark-fs-stat.c
  benchmark-getaddrinfo.c
  benchmark-loop-count.c
  benchmark-million-async.c
  benchmark-million-timers.c
  benchmark-multi-accept.c
  benchmark-ping-pongs.c
  benchmark-ping-udp.c
  benchmark-pound.c
  benchmark-pump.c
  benchmark-sizes.c
  benchmark-spawn.c
  benchmark-tcp-write-batch.c
  benchmark-thread.c
  benchmark-udp-pummel.c
  blackhole-server.c
  dns-server.c
  echo-server.c
  run-benchmarks.c
  runner.c)

target_compile_definitions(uv_run_benchmarks_a PRIVATE ${uv_defines})
target_compile_options(uv_run_benchmarks_a PRIVATE ${uv_cflags})
target_link_libraries(uv_run_benchmarks_a PRIVATE uv_a ${uv_test_libraries})

list(APPEND uv_test_sources
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
     test-tty-escape-sequence-processing.c
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
     test-udp-sendmmsg-error.c
     test-udp-send-unreachable.c
     test-udp-try-send.c
     test-uname.c
     test-walk-handles.c
     test-watcher-cross-stop.c)

add_executable(uv_run_tests ${uv_test_sources} uv_win_longpath.manifest)
target_compile_definitions(uv_run_tests
                           PRIVATE ${uv_defines} USING_UV_SHARED=1)
target_compile_options(uv_run_tests PRIVATE ${uv_cflags})
target_link_libraries(uv_run_tests uv ${uv_test_libraries})

add_test(NAME uv_test
         COMMAND uv_run_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
  set_tests_properties(uv_test PROPERTIES ENVIRONMENT
                       "LIBPATH=${CMAKE_BINARY_DIR}:$ENV{LIBPATH}")
endif()
add_executable(uv_run_tests_a ${uv_test_sources} uv_win_longpath.manifest)
target_compile_definitions(uv_run_tests_a PRIVATE ${uv_defines})
target_compile_options(uv_run_tests_a PRIVATE ${uv_cflags})
target_link_libraries(uv_run_tests_a PRIVATE uv_a ${uv_test_libraries} $<$<BOOL:${QEMU}>:-static>)
add_test(NAME uv_test_a
         COMMAND uv_run_tests_a
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
