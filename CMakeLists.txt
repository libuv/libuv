cmake_minimum_required(VERSION 2.8)

# set(LIBUVDIR ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(include src)
set(SOURCES
  src/fs-poll.c
  src/inet.c
  src/loop-watcher.c
  src/threadpool.c
  src/timer.c
  src/uv-common.c
  src/version.c)

if(WIN32)
  add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0600 -D_CRT_SECURE_NO_WARNINGS)
  include_directories(src/win)
  set(SOURCES ${SOURCES}
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
    src/win/pipe.c
    src/win/poll.c
    src/win/process.c
    src/win/process-stdio.c
    src/win/req.c
    src/win/signal.c
    src/win/stream.c
    src/win/tcp.c
    src/win/thread.c
    src/win/tty.c
    src/win/udp.c
    src/win/util.c
    src/win/winapi.c
    src/win/winsock.c)
else()
  if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_definitions(-D_GNU_SOURCE)
    set(SOURCES ${SOURCES}
      src/unix/linux-syscalls.c
      src/unix/linux-core.c
      src/unix/linux-inotify.c)
  elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    add_definitions(-D_DARWIN_USE_64_BIT_INODE=1 -D_DARWIN_UNLIMITED_SELECT=1)
    set(SOURCES ${SOURCES}
      src/unix/darwin.c
      src/unix/darwin-proctitle.c
      src/unix/fsevents.c
      src/unix/kqueue.c
      src/unix/proctitle.c)
  elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(SOURCES ${SOURCES}
      src/unix/freebsd.c)
  elseif(${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    set(SOURCES ${SOURCES}
      src/unix/openbsd.c)
  elseif(${CMAKE_SYSTEM_NAME} STREQUAL "NetBSD")
    set(SOURCES ${SOURCES}
      src/unix/kqueue.c
      src/unix/netbsd.c)
  elseif(${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
    add_definitions(-D__EXTENSIONS__ -D_XOPEN_SOURCE=500)
    set(SOURCES ${SOURCES}
      src/unix/sunos.c)
  endif()

  include_directories(src/unix)
  set(SOURCES ${SOURCES}
    src/unix/async.c
    src/unix/core.c
    src/unix/dl.c
    src/unix/fs.c
    src/unix/getaddrinfo.c
    src/unix/getnameinfo.c
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
endif()

add_library(uv ${SOURCES})

find_package(Threads)

if(WIN32)
  target_link_libraries(uv ws2_32 psapi iphlpapi shell32 userenv)
else()
  target_link_libraries(uv ${CMAKE_THREAD_LIBS_INIT})
endif()

if (BUILD_SHARED_LIBS)
  target_compile_definitions(
    uv
    PRIVATE BUILDING_UV_SHARED
    INTERFACE USING_UV_SHARED)
endif()

install(TARGETS uv
  EXPORT libuvConfig
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT libuvConfig DESTINATION lib/cmake/libuv)
