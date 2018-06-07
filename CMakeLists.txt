# TODO: determine CMAKE_SYSTEM_NAME on OS/390.  Currently assumes "OS/390".
cmake_minimum_required(VERSION 3.0)
project(libuv)

option(BUILD_TESTING "Enable building of libuv tests" On)
option(BUILD_BENCHMARKING "Enable building of libuv benchmarking (testing must be enabled)" On)

if(MSVC)
  list(APPEND uv_cflags /W4)
elseif(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  list(APPEND uv_cflags -fvisibility=hidden --std=gnu89)
  list(APPEND uv_cflags -Wall -Wextra -Wstrict-prototypes)
  list(APPEND uv_cflags -Wno-unused-parameter)
endif()

file(GLOB uv_sources src/*.c)

if(BUILD_TESTING)
  enable_testing()
  list(APPEND uv_test_common_sources
       test/blackhole-server.c
       test/echo-server.c
       test/runner.c)
  file(GLOB uv_test_sources test/test-*.c)

  if(BUILD_BENCHMARKING)
    file(GLOB uv_benchmark_sources test/benchmark-*.c)
  endif()
endif()

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
  file(GLOB uv_win_sources src/win/*.c)
  list(APPEND uv_sources ${uv_win_sources})
  list(APPEND uv_test_libraries ws2_32)
  if(BUILD_TESTING)
    list(APPEND uv_test_common_sources src/win/snprintf.c test/runner-win.c)
    if(BUILD_BENCHMARKING)
      list(APPEND uv_test_common_sources src/win/snprintf.c test/runner-win.c)
    endif()
  endif()
else()
  list(APPEND uv_defines _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
  list(APPEND uv_libraries pthread)
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
       src/unix/timer.c
       src/unix/tty.c
       src/unix/udp.c)
  list(APPEND uv_test_common_sources test/runner-unix.c)
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
  list(APPEND uv_sources src/unix/posix-hrtime.c)
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
  add_executable(uv_run_tests ${uv_test_sources} ${uv_test_common_sources} test/run-tests.c)
  target_compile_definitions(uv_run_tests PRIVATE ${uv_defines} USING_UV_SHARED=1)
  target_compile_options(uv_run_tests PRIVATE ${uv_cflags})
  target_include_directories(uv_run_tests PRIVATE include)
  target_link_libraries(uv_run_tests uv ${uv_test_libraries})
  add_test(NAME uv_test
           COMMAND uv_run_tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(uv_run_tests_a ${uv_test_sources} ${uv_test_common_sources} test/run-tests.c)
  target_compile_definitions(uv_run_tests_a PRIVATE ${uv_defines})
  target_compile_options(uv_run_tests_a PRIVATE ${uv_cflags})
  target_include_directories(uv_run_tests_a PRIVATE include)
  target_link_libraries(uv_run_tests_a uv_a ${uv_test_libraries})
  add_test(NAME uv_test_a
           COMMAND uv_run_tests_a
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  if(BUILD_BENCHMARKING)
    add_executable(uv_run_benchmarks ${uv_test_common_sources} ${uv_benchmark_sources} test/run-benchmarks.c)
    target_compile_definitions(uv_run_benchmarks PRIVATE ${uv_defines} USING_UV_SHARED=1)
    target_compile_options(uv_run_benchmarks PRIVATE ${uv_cflags})
    target_include_directories(uv_run_benchmarks PRIVATE include)
    target_link_libraries(uv_run_benchmarks uv ${uv_test_libraries})
    add_test(NAME uv_benchmark
             COMMAND uv_run_benchmarks
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(uv_run_benchmarks_a ${uv_test_common_sources} ${uv_benchmark_sources} test/run-benchmarks.c)
    target_compile_definitions(uv_run_benchmarks_a PRIVATE ${uv_defines})
    target_compile_options(uv_run_benchmarks_a PRIVATE ${uv_cflags})
    target_include_directories(uv_run_benchmarks_a PRIVATE include)
    target_link_libraries(uv_run_benchmarks_a uv_a ${uv_test_libraries})
    add_test(NAME uv_benchmarks_a
             COMMAND uv_run_benchmarks_a
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
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
  install(FILES LICENSE ${CMAKE_CURRENT_BINARY_DIR}/libuv.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(TARGETS uv LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(TARGETS uv_a ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
elseif(WIN32)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_PREFIX})
  install(TARGETS uv
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin  # for dll/executable files
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib  # for shared library
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib) # for static library
  install(TARGETS uv_a
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib) # for static library
  install(FILES $<TARGET_PDB_FILE:uv> DESTINATION ${CMAKE_INSTALL_PREFIX}/bin OPTIONAL)
endif()
