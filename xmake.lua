add_includedirs("include","src")
add_files(
	--'include/uv.h',
	--'include/uv/tree.h',
	--'include/uv/errno.h',
	--'include/uv/threadpool.h',
	--'include/uv/version.h',
	'src/fs-poll.c',
	--'src/heap-inl.h',
	'src/inet.c',
	--'src/queue.h',
	'src/threadpool.c',
	'src/timer.c',
	'src/uv-data-getter-setters.c',
	'src/uv-common.c',
	--'src/uv-common.h',
	'src/version.c'
)
if is_plat("windows") then
	add_defines('_WIN32_WINNT=0x0600','_GNU_SOURCE')
	add_files(
			--'include/uv/win.h',
            'src/win/async.c',
            --'src/win/atomicops-inl.h',
            'src/win/core.c',
            'src/win/detect-wakeup.c',
            'src/win/dl.c',
            'src/win/error.c',
            'src/win/fs.c',
            'src/win/fs-event.c',
            'src/win/getaddrinfo.c',
            'src/win/getnameinfo.c',
            'src/win/handle.c',
            --'src/win/handle-inl.h',
            --'src/win/internal.h',
            'src/win/loop-watcher.c',
            'src/win/pipe.c',
            'src/win/thread.c',
            'src/win/poll.c',
            'src/win/process.c',
            'src/win/process-stdio.c',
            'src/win/req.c',
            --'src/win/req-inl.h',
            'src/win/signal.c',
            'src/win/snprintf.c',
            'src/win/stream.c',
            --'src/win/stream-inl.h',
            'src/win/tcp.c',
            'src/win/tty.c',
            'src/win/udp.c',
            'src/win/util.c',
            'src/win/winapi.c',
            --'src/win/winapi.h',
            'src/win/winsock.c'
            --'src/win/winsock.h'
	)
	add_links(
		'advapi32',
		'iphlpapi',
		'psapi',
		'shell32',
		'user32',
		'userenv',
		'ws2_32'
	)
else
	add_defines("_LARGEFILE_SOURCE","_FILE_OFFSET_BITS=64") --shared_unix_defines
	if is_plat("macosx","iphoneos") then
		add_defines("_DARWIN_USE_64_BIT_INODE=1") --shared_mac_defines
		add_defines('_DARWIN_USE_64_BIT_INODE=1','_DARWIN_UNLIMITED_SELECT=1')
	end
	if is_plat("zos") then
		add_defines(
			'_UNIX03_THREADS',
			'_UNIX03_SOURCE',
			'_UNIX03_WITHDRAWN',
			'_OPEN_SYS_IF_EXT',
			'_OPEN_SYS_SOCK_IPV6',
			'_OPEN_MSGQ_EXT',
			'_XOPEN_SOURCE_EXTENDED',
			'_ALL_SOURCE',
			'_LARGE_TIME_API',
			'_OPEN_SYS_FILE_EXT',
			'_AE_BIMODAL',
			'PATH_MAX=255'
			) --shared_zos_defines
	end
	if is_plat("linux") then
		add_defines("_POSIX_C_SOURCE=200112")
		add_defines('_GNU_SOURCE')
	end
	if is_plat("solaris") then
		add_defines('__EXTENSIONS__','_XOPEN_SOURCE=500')
	end
	if is_plat("aix") then
		local SystemName = os.iorun("uname -s")
		if SystemName ~= "OS400" then
			add_defines("HAVE_SYS_AHAFS_EVPRODS_H")
		end
	end
	add_files(
		--'include/uv/unix.h',
		--'include/uv/linux.h',
		--'include/uv/sunos.h',
		--'include/uv/darwin.h',
		--'include/uv/bsd.h',
		--'include/uv/aix.h',
		'src/unix/async.c',
		--'src/unix/atomic-ops.h',
		'src/unix/core.c',
		'src/unix/dl.c',
		'src/unix/fs.c',
		'src/unix/getaddrinfo.c',
		'src/unix/getnameinfo.c',
		--'src/unix/internal.h',
		'src/unix/loop.c',
		'src/unix/loop-watcher.c',
		'src/unix/pipe.c',
		'src/unix/poll.c',
		'src/unix/process.c',
		'src/unix/signal.c',
		--'src/unix/spinlock.h',
		'src/unix/stream.c',
		'src/unix/tcp.c',
		'src/unix/thread.c',
		'src/unix/tty.c',
		'src/unix/udp.c'
	)
	if is_plat("linux","macosx","iphoneos","android","zos") then --'OS in "linux mac ios android zos"
		add_files("src/unix/proctitle.c")
	end
	if is_plat("macosx","iphoneos") then
		add_files(
			'src/unix/darwin.c',
            'src/unix/fsevents.c',
            'src/unix/darwin-proctitle.c'
		)
	end
	if is_plat("linux") then
		add_files(
			'src/unix/linux-core.c',
            'src/unix/linux-inotify.c',
            'src/unix/linux-syscalls.c',
            --'src/unix/linux-syscalls.h',
            'src/unix/procfs-exepath.c',
            'src/unix/sysinfo-loadavg.c',
            'src/unix/sysinfo-memory.c'
		)
	end
	if is_plat("android") then
		add_files(
			'src/unix/linux-core.c',
            'src/unix/linux-inotify.c',
            'src/unix/linux-syscalls.c',
            --'src/unix/linux-syscalls.h',
            'src/unix/pthread-fixes.c',
            'src/unix/android-ifaddrs.c',
            'src/unix/procfs-exepath.c',
            'src/unix/sysinfo-loadavg.c',
            'src/unix/sysinfo-memory.c'
		)
	end
	if is_plat("solaris") then
		add_files(
			'src/unix/no-proctitle.c',
            'src/unix/sunos.c'
		)
	end
	if is_plat("aix") then
		local SystemName = os.iorun("uname -s")
		if SystemName == "OS400" then
			add_files(
				'src/unix/ibmi.c',
                'src/unix/posix-poll.c',
                'src/unix/no-fsevents.c',
                'src/unix/no-proctitle.c'
			)
		else
			add_files('src/unix/aix.c')
		end
	end
	if is_plat("freebsd", "dragonflybsd") then
		add_files("src/unix/freebsd.c")
	end
	if is_plat("openbsd") then
		add_files("src/unix/openbsd.c")
	end
	if is_plat("netbsd") then
		add_files("src/unix/netbsd.c")
	end
	if is_plat("freebsd", "dragonflybsd", "openbsd", "netbsd") then
		add_files("src/unix/posix-hrtime.c")
	end
	if is_plat("ios", "mac", "freebsd", "dragonflybsd", "openbsd", "netbsd") then
		add_files(
			'src/unix/bsd-ifaddrs.c',
            'src/unix/kqueue.c'
		)
	end
	if is_plat("zos") then
		add_files(
			'src/unix/pthread-fixes.c',
            'src/unix/os390.c',
            'src/unix/os390-syscalls.c'
		)
	end
	add_links("m")
	if is_plat("solaris") then
		add_ldflags("-pthreads")
	end
	if is_plat("zos") and is_kind("shared") then
		add_ldflags("-Wl,DLL")
	end
	if not(is_plat("solaris","android","zos")) then
		add_ldflags("-pthread")
	end
	if is_kind("shared") then
		if is_plat("zos") then
			add_cflags("-qexportall")
		else
			add_cflags("-fPIC")
		end
		if not(is_plat("macosx","zos")) then
			--Set extention to "so.VER_MAJOR"
		end
	end
	if not(is_plat("zos")) then
		add_cflags(
			'-fvisibility=hidden',
            '-g',
            '--std=gnu89',
            '-pedantic',
            '-Wall',
            '-Wextra',
            '-Wno-unused-parameter',
            '-Wstrict-prototypes'
		)
	end
	if is_plat("linux") then
		add_links("dl", "rt")
	end
	if is_plat("android") then
		add_links("dl")
	end
	if is_plat("solaris") then
		add_links(
			'kstat',
			'nsl',
			'sendfile',
			'socket'
		)
	end
	if is_plat("aix") then
		local SystemName = os.iorun("uname -s")
		if SystemName ~= "OS400" then
			add_links("perfstat")
		end
	end
	if is_plat("netbsd") then
		add_links("kvm")
	end
end
target("uv")
	set_kind("$(kind)")
	if is_kind("shared") then
		add_defines("BUILDING_UV_SHARED=1");
	end
	