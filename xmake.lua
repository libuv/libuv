-- project
set_project("libuv")

-- version
set_version("1.3.1")

-- xmake version
set_xmakever("2.1.5")

-- set warnings 
if is_plat("windows", "android") then
    set_warnings("all")
else
    set_warnings("all", "error")
end

-- set language
if not is_plat("windows") then
    set_languages("gnu99")
end

-- disable some compiler errors
add_cflags("-Wno-error=nullability-completeness")
if is_plat("sunos") then
    add_cflags("-pthreads")
end

-- add headers and includes
add_includedirs("include", "src")
add_headers("(include/uv.h)", "(include/uv-errno.h)", "(include/uv-threadpool.h)", "(include/uv-version.h)")

-- set the output directory of the object and target files
set_objectdir("$(buildir)/$(mode)/$(arch)/.objs")
set_targetdir("$(buildir)/$(mode)/$(arch)")

-- the debug mode
if is_mode("debug") then
    
   -- enable the debug symbols
    set_symbols("debug")

    -- disable optimization
    set_optimize("none")

    -- for windows platform
    if is_plat("windows") then
        add_cflags("-Gs", "-RTC1", "-MTd") 
    end
end

-- the release mode
if is_mode("release") then

    -- set the symbols visibility: hidden
    set_symbols("hidden")

    -- strip all symbols
    set_strip("all")

    -- fomit the frame pointer
    add_cflags("-fomit-frame-pointer")

    -- for windows platform
    if is_plat("windows") then
        add_cflags("-MT") 
    end
end

-- for windows platform
if is_plat("windows") then

    -- add defines
    add_defines("WIN32_LEAN_AND_MEAN", "_WIN32_WINNT=0x0600")

    -- no msvcrt.lib
    add_ldflags("-nodefaultlib:\"msvcrt.lib\"")
end

-- define target: libuv
target("uv") 
    
    -- set kind: static library
    set_kind("static")

    -- for common platform
    add_files("src/*.c")

    -- for unix platform
    if not is_plat("windows") then
        add_files("src/unix/async.c") 
        add_files("src/unix/core.c") 
        add_files("src/unix/dl.c") 
        add_files("src/unix/fs.c") 
        add_files("src/unix/getaddrinfo.c") 
        add_files("src/unix/getnameinfo.c") 
        add_files("src/unix/loop.c") 
        add_files("src/unix/pipe.c") 
        add_files("src/unix/poll.c") 
        add_files("src/unix/process.c") 
        add_files("src/unix/signal.c") 
        add_files("src/unix/stream.c") 
        add_files("src/unix/tcp.c") 
        add_files("src/unix/thread.c") 
        add_files("src/unix/tty.c") 
        add_files("src/unix/udp.c")
        add_includedirs("src/unix")
        add_headers("(include/uv-unix.h)")
    end

    -- for windows platform
    if is_plat("windows") then
        add_files("src/win/*.c")
        add_includedirs("src/win")
        add_headers("(include/uv-win.h)", "(include/tree.h)")
    end

    -- for aix platform
    if is_plat("aix") then
        add_files("src/unix/aix.c")
        add_headers("(include/uv-aix.h)")
        add_defines("_ALL_SOURCE", "_XOPEN_SOURCE=500", "_LINUX_SOURCE_COMPAT", "_THREAD_SAFE", "HAVE_SYS_AHAFS_EVPRODS_H")
    end

    -- for android platform
    if is_plat("android") then
        add_files("src/unix/android-ifaddrs.c")
        add_files("src/unix/pthread-fixes.c")
        add_files("src/unix/pthread-barrier.c")
        add_headers("(include/android-ifaddrs.h)", "(include/pthread-barrier.h)")
    end

    -- for cygwin/msys platform
    if is_plat("cygwin", "msys") then
        add_files("src/unix/cygwin.c") 
        add_files("src/unix/bsd-ifaddrs.c") 
        add_files("src/unix/no-fsevents.c") 
        add_files("src/unix/no-proctitle.c") 
        add_files("src/unix/posix-hrtime.c") 
        add_files("src/unix/posix-poll.c") 
        add_files("src/unix/procfs-exepath.c") 
        add_files("src/unix/sysinfo-loadavg.c") 
        add_files("src/unix/sysinfo-memory.c")
        add_defines("_GNU_SOURCE")
    end

    -- for macosx/iphoneos/watchos platform
    if is_plat("macosx", "iphoneos", "watchos") then
        add_files("src/unix/bsd-ifaddrs.c") 
        add_files("src/unix/darwin.c") 
        add_files("src/unix/darwin-proctitle.c") 
        add_files("src/unix/fsevents.c") 
        add_files("src/unix/kqueue.c") 
        add_files("src/unix/proctitle.c") 
        add_files("src/unix/pthread-barrier.c")
        add_defines("_DARWIN_USE_64_BIT_INODE=1", "_DARWIN_UNLIMITED_SELECT=1")
        add_headers("(include/uv-darwin.h)", "(include/pthread-barrier.h)")
    end

    -- for gragonfly/freebsd/netbsd/openbsd platform
    if is_plat("gragonfly", "freebsd", "netbsd", "openbsd") then
        add_files("src/unix/bsd-ifaddrs.c") 
        add_files("src/unix/freebsd.c") 
        add_files("src/unix/kqueue.c") 
        add_files("src/unix/posix-hrtime.c") 
        add_headers("(include/uv-bsd.h)")
    end

    -- for linux/android platform
    if is_plat("linux", "android") then
        add_files("src/unix/linux-core.c") 
        add_files("src/unix/linux-inotify.c") 
        add_files("src/unix/linux-syscalls.c") 
        add_files("src/unix/procfs-exepath.c") 
        add_files("src/unix/proctitle.c") 
        add_files("src/unix/sysinfo-loadavg.c") 
        add_files("src/unix/sysinfo-memory.c")
        add_defines("_GNU_SOURCE")
        add_headers("(include/uv-linux.h)")
    end

    -- for sunos platform
    if is_plat("sunos") then
        add_files("src/unix/no-proctitle.c")
        add_files("src/unix/sunos.c")
        add_defines("__EXTENSIONS_", "_XOPEN_SOURCE=600")
        add_headers("(include/uv-sunos.h)")
    end

    -- for os390 platform
    if is_plat("os390") then
        add_files("src/unix/pthread-fixes.c") 
        add_files("src/unix/pthread-barrier.c") 
        add_files("src/unix/no-fsevents.c") 
        add_files("src/unix/os390.c") 
        add_files("src/unix/os390-syscalls.c") 
        add_files("src/unix/proctitle.c")
        add_headers("(include/pthread-fixes.h)", "(include/pthread-barrier.h)")
        add_ldflags("-qXPLINK")
        add_cflags("-qCHARS=signed", "-qXPLINK", "-qFLOAT=IEEE")
        add_defines("_UNIX03_THREADS", "_UNIX03_SOURCE", "_OPEN_SYS_IF_EXT=1", "_OPEN_MSGQ_EXT", "_XOPEN_SOURCE_EXTENDED")
        add_defines("_ALL_SOURCE", "_LARGE_TIME_API", "_OPEN_SYS_SOCK_IPV6", "_OPEN_SYS_FILE_EXT", "UV_PLATFORM_SEM_T=int", "PATH_MAX=255")
    end

-- define target: test
target("test")
    
    -- set kind: binary
    set_kind("binary")

    -- disable build by default
    -- 
    -- please run `xmake -b test` or `xmake -a` if want to build it or all
    -- 
    -- or run tests directly by `xmake run test`
    -- 
    set_default(false)

    -- add deps
    add_deps("uv")

    -- add sources
    add_files("test/blackhole-server.c") 
    add_files("test/dns-server.c") 
    add_files("test/echo-server.c") 
    add_files("test/run-tests.c") 
    add_files("test/runner.c") 
    add_files("test/test-active.c") 
    add_files("test/test-async.c") 
    add_files("test/test-async-null-cb.c") 
    add_files("test/test-barrier.c") 
    add_files("test/test-callback-order.c") 
    add_files("test/test-callback-stack.c") 
    add_files("test/test-close-fd.c") 
    add_files("test/test-close-order.c") 
    add_files("test/test-condvar.c") 
    add_files("test/test-connection-fail.c") 
    add_files("test/test-cwd-and-chdir.c") 
    add_files("test/test-default-loop-close.c") 
    add_files("test/test-delayed-accept.c") 
    add_files("test/test-dlerror.c") 
    add_files("test/test-eintr-handling.c") 
    add_files("test/test-embed.c") 
    add_files("test/test-emfile.c") 
    add_files("test/test-env-vars.c") 
    add_files("test/test-error.c") 
    add_files("test/test-fail-always.c") 
    add_files("test/test-fs-event.c") 
    add_files("test/test-fs-poll.c") 
    add_files("test/test-fs.c") 
    add_files("test/test-fork.c") 
    add_files("test/test-get-currentexe.c") 
    add_files("test/test-get-loadavg.c") 
    add_files("test/test-get-memory.c") 
    add_files("test/test-get-passwd.c") 
    add_files("test/test-getaddrinfo.c") 
    add_files("test/test-gethostname.c") 
    add_files("test/test-getnameinfo.c") 
    add_files("test/test-getsockname.c") 
    add_files("test/test-handle-fileno.c") 
    add_files("test/test-homedir.c") 
    add_files("test/test-hrtime.c") 
    add_files("test/test-idle.c") 
    add_files("test/test-ip4-addr.c") 
    add_files("test/test-ip6-addr.c") 
    add_files("test/test-ipc-send-recv.c") 
    add_files("test/test-ipc.c") 
    add_files("test/test-loop-handles.c") 
    add_files("test/test-loop-alive.c") 
    add_files("test/test-loop-close.c") 
    add_files("test/test-loop-stop.c") 
    add_files("test/test-loop-time.c") 
    add_files("test/test-loop-configure.c") 
    add_files("test/test-multiple-listen.c") 
    add_files("test/test-mutexes.c") 
    add_files("test/test-osx-select.c") 
    add_files("test/test-pass-always.c") 
    add_files("test/test-ping-pong.c") 
    add_files("test/test-pipe-bind-error.c") 
    add_files("test/test-pipe-connect-error.c") 
    add_files("test/test-pipe-connect-multiple.c") 
    add_files("test/test-pipe-connect-prepare.c") 
    add_files("test/test-pipe-getsockname.c") 
    add_files("test/test-pipe-pending-instances.c") 
    add_files("test/test-pipe-sendmsg.c") 
    add_files("test/test-pipe-server-close.c") 
    add_files("test/test-pipe-close-stdout-read-stdin.c") 
    add_files("test/test-pipe-set-non-blocking.c") 
    add_files("test/test-platform-output.c") 
    add_files("test/test-poll-close.c") 
    add_files("test/test-poll-close-doesnt-corrupt-stack.c") 
    add_files("test/test-poll-closesocket.c") 
    add_files("test/test-poll.c") 
    add_files("test/test-process-title.c") 
    add_files("test/test-queue-foreach-delete.c") 
    add_files("test/test-ref.c") 
    add_files("test/test-run-nowait.c") 
    add_files("test/test-run-once.c") 
    add_files("test/test-semaphore.c") 
    add_files("test/test-shutdown-close.c") 
    add_files("test/test-shutdown-eof.c") 
    add_files("test/test-shutdown-twice.c") 
    add_files("test/test-signal-multiple-loops.c") 
    add_files("test/test-signal.c") 
    add_files("test/test-socket-buffer-size.c") 
    add_files("test/test-spawn.c") 
    add_files("test/test-stdio-over-pipes.c") 
    add_files("test/test-tcp-alloc-cb-fail.c") 
    add_files("test/test-tcp-bind-error.c") 
    add_files("test/test-tcp-bind6-error.c") 
    add_files("test/test-tcp-close-accept.c") 
    add_files("test/test-tcp-close-while-connecting.c") 
    add_files("test/test-tcp-close.c") 
    add_files("test/test-tcp-create-socket-early.c") 
    add_files("test/test-tcp-connect-error-after-write.c") 
    add_files("test/test-tcp-connect-error.c") 
    add_files("test/test-tcp-connect-timeout.c") 
    add_files("test/test-tcp-connect6-error.c") 
    add_files("test/test-tcp-flags.c") 
    add_files("test/test-tcp-open.c") 
    add_files("test/test-tcp-read-stop.c") 
    add_files("test/test-tcp-shutdown-after-write.c") 
    add_files("test/test-tcp-unexpected-read.c") 
    add_files("test/test-tcp-oob.c") 
    add_files("test/test-tcp-write-to-half-open-connection.c") 
    add_files("test/test-tcp-write-after-connect.c") 
    add_files("test/test-tcp-writealot.c") 
    add_files("test/test-tcp-write-fail.c") 
    add_files("test/test-tcp-try-write.c") 
    add_files("test/test-tcp-write-queue-order.c") 
    add_files("test/test-thread-equal.c") 
    add_files("test/test-thread.c") 
    add_files("test/test-threadpool-cancel.c") 
    add_files("test/test-threadpool.c") 
    add_files("test/test-timer-again.c") 
    add_files("test/test-timer-from-check.c") 
    add_files("test/test-timer.c") 
    add_files("test/test-tmpdir.c") 
    add_files("test/test-tty.c") 
    add_files("test/test-udp-alloc-cb-fail.c") 
    add_files("test/test-udp-bind.c") 
    add_files("test/test-udp-create-socket-early.c") 
    add_files("test/test-udp-dgram-too-big.c") 
    add_files("test/test-udp-ipv6.c") 
    add_files("test/test-udp-multicast-interface.c") 
    add_files("test/test-udp-multicast-interface6.c") 
    add_files("test/test-udp-multicast-join.c") 
    add_files("test/test-udp-multicast-join6.c") 
    add_files("test/test-udp-multicast-ttl.c") 
    add_files("test/test-udp-open.c") 
    add_files("test/test-udp-options.c") 
    add_files("test/test-udp-send-and-recv.c") 
    add_files("test/test-udp-send-hang-loop.c") 
    add_files("test/test-udp-send-immediate.c") 
    add_files("test/test-udp-send-unreachable.c") 
    add_files("test/test-udp-try-send.c") 
    add_files("test/test-walk-handles.c") 
    add_files("test/test-watcher-cross-stop.c")

    -- for windows/unix platform
    if is_plat("windows") then
        add_files("test/runner-win.c")
        add_links("ws2_32", "iphlpapi", "advapi32", "user32", "userenv", "psapi", "shell32")
    else
        add_files("test/runner-unix.c")
    end

    -- for aix platform
    if is_plat("aix") then
        add_defines("_ALL_SOURCE", "_XOPEN_SOURCE=500", "_LINUX_SOURCE_COMPAT")
    end

    -- for linux/android platform
    if is_plat("linux", "android") then
        add_defines("_GNU_SOURCE")
    end

    -- for sunos platform
    if is_plat("sunos") then
        add_defines("__EXTENSIONS__", "_XOPEN_SOURCE=600")
    end

    -- for os390 platform
    if is_plat("os390") then
        add_ldflags("-qXPLINK")
        add_cflags("-qCHARS=signed", "-qXPLINK", "-qFLOAT=IEEE")
        add_defines("_UNIX03_THREADS", "_UNIX03_SOURCE", "_OPEN_SYS_IF_EXT=1", "_OPEN_MSGQ_EXT", "_XOPEN_SOURCE_EXTENDED")
        add_defines("_ALL_SOURCE", "_LARGE_TIME_API", "_OPEN_SYS_SOCK_IPV6", "_OPEN_SYS_FILE_EXT", "UV_PLATFORM_SEM_T=int", "PATH_MAX=255")
    else
        add_cflags("-Wno-long-long")
    end

    -- add links: -lutil
    if is_plat("macosx", "iphoneos", "watchos", "DRAGONFLY", "FREEBSD", "linux", "netbsd", "freebsd") then
        add_links("util")
    end



