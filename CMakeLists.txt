###############################################################################
# CMake build for libuv
# 
# 20 Jul 2016
# Colin Taylor <cjntaylor@gmail.com>
###############################################################################

cmake_minimum_required(VERSION 3.0)

project(libuv)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, \
        options are: Debug Release RelWithDebInfo MinSizeRel." FORCE
    )
endif()

set(UV_LIBRARY_NAME uv)
set(UV_VERSION_MAJOR 1)
set(UV_VERSION_MINOR 9)
set(UV_VERSION_PATCH 2)
set(UV_VERSION ${UV_VERSION_MAJOR}.${UV_VERSION_MINOR}.${UV_VERSION_PATCH})
set(UV_VERSION_SOVER ${UV_VERSION_MAJOR})

option(UV_TESTS_ENABLED "Enable testing" Off)
option(UV_BUILD_STATIC "Build static library" On)
option(UV_BUILD_SHARED "Build shared library" On)
if(APPLE)
    option(UV_BUILD_FRAMEWORK "Build OSX framework" On)
endif()

# Catch invalid configuration state (no library types)
if(NOT UV_BUILD_SHARED AND NOT UV_BUILD_STATIC AND NOT UV_BUILD_FRAMEWORK)
    message(FATAL_ERROR "No library set to build")
endif()

# Can't install when shared or static is not built
if(NOT UV_BUILD_SHARED OR UV_BUILD_STATIC)
    set(UV_INSTALL_ENABLED False CACHE STRING "" FORCE)
endif()

# Generate missing platform variables based on the system name
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CMAKE_SYSTEM_NAME_LOWER)
if(CMAKE_SYSTEM_NAME_LOWER STREQUAL "linux")
    set(LINUX True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "freebsd")
    set(BSD True)
    set(FREEBSD True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "dragonfly")
    set(BSD True)
    set(DRAGONFLYBSD True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "netbsd")
    set(BSD True)
    set(NETBSD True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "openbsd")
    set(BSD True)
    set(OPENBSD True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "aix")
    set(AIX True)
elseif(CMAKE_SYSTEM_NAME_LOWER STREQUAL "sunos")
    set(SOLARIS True)
endif()

# Add the local cmake folder to the module search path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Make sure we're building out of source
include(EnsureOutOfSourceBuild)
ensure_out_of_source_build("\
    ${PROJECT_NAME} requires an out of source build. \
    Please create a separate build directory and run \
    'cmake /path/to/${PROJECT_NAME} [options]' there.\
")

# Common public headers across all platforms
list(APPEND UV_HEADERS
    include/uv.h
    include/tree.h
    include/uv-errno.h
    include/uv-threadpool.h
    include/uv-version.h
    include/uv-win.h
    include/uv-unix.h
    include/uv-linux.h
    include/uv-sunos.h
    include/uv-darwin.h
    include/uv-bsd.h
    include/uv-aix.h
    include/stdint-msvc2008.h
    include/pthread-barrier.h
    include/android-ifaddrs.h
)

# Common private headers across all platforms
list(APPEND UV_PRIVATE_HEADERS
    src/heap-inl.h
    src/queue.h
    src/uv-common.h
)

# Common sources across all platforms
list(APPEND UV_SOURCES
    src/fs-poll.c
    src/inet.c
    src/threadpool.c
    src/uv-common.c
    src/version.c
)

# Standard include paths
# Specified separately for build and install for EXPORT integration
list(APPEND UV_INCLUDE
    PUBLIC  
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Set debug defines (using generator expressions)
list(APPEND UV_DEFINES
    PRIVATE $<$<CONFIG:Debug>:DEBUG>
    PRIVATE $<$<CONFIG:Debug>:_DEBUG>
    PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# Set the install path per platform
if(WIN32)
    # Use C:\Program Files\${PROJECT_NAME}\<version> under windows (since there
    # isn't a common "lib" folder)
    set(UV_INSTALL_PATH ${PROJECT_NAME}/${UV_VERSION})
    set(UV_LIBRARY_INSTALL_PATH ${UV_INSTALL_PATH})
    set(UV_RUNTIME_INSTALL_PATH ${UV_INSTALL_PATH})
    set(UV_INCLUDE_INSTALL_PATH ${UV_INSTALL_PATH}/include)
    set(UV_PACKAGE_INSTALL_PATH cmake/${PROJECT_NAME})
else()
    set(UV_LIBRARY_INSTALL_PATH lib)
    # Use library architecture if it is set (multiarch support)
    if(CMAKE_LIBRARY_ARCHITECTURE)
        set(UV_LIBRARY_INSTALL_PATH
            "${UV_LIBRARY_INSTALL_PATH}/${CMAKE_LIBRARY_ARCHITECTURE}"
        )
    endif()
    set(UV_RUNTIME_INSTALL_PATH bin)
    set(UV_INCLUDE_INSTALL_PATH include)
    set(UV_PACKAGE_INSTALL_PATH 
        ${UV_LIBRARY_INSTALL_PATH}/cmake/${PROJECT_NAME}
    )
endif()

# Generate output tree
if(NOT CMAKE_OUTPUT_DIRECTORY)
    set(CMAKE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
set(CMAKE_INCLUDE_OUTPUT_DIRECTORY 
    ${CMAKE_OUTPUT_DIRECTORY}/${UV_INCLUDE_INSTALL_PATH}
)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY 
    ${CMAKE_OUTPUT_DIRECTORY}/${UV_LIBRARY_INSTALL_PATH}
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY 
    ${CMAKE_OUTPUT_DIRECTORY}/${UV_RUNTIME_INSTALL_PATH}
)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY 
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
)
set(CMAKE_PACKAGE_OUTPUT_DIRECTORY 
    ${CMAKE_OUTPUT_DIRECTORY}/${UV_PACKAGE_INSTALL_PATH}
)

# Building for windows
if(WIN32)
    # Differentiate .lib names under windows for static libraries
    # (Since the dll also produces a .lib file)
    set(CMAKE_STATIC_LIBRARY_SUFFIX "_static${CMAKE_STATIC_LIBRARY_SUFFIX}")

    # Windows private headers
    list(APPEND UV_PRIVATE_HEADERS
        src/win/atomicops-inl.h
        src/win/handle-inl.h
        src/win/internal.h
        src/win/req-inl.h
        src/win/stream-inl.h
        src/win/winapi.h
        src/win/winsock.h
    )

    # Windows sources
    list(APPEND UV_SOURCES
        src/win/async.c
        src/win/core.c
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
        src/win/req.c
        src/win/signal.c
        src/win/snprintf.c
        src/win/stream.c
        src/win/tcp.c
        src/win/tty.c
        src/win/timer.c
        src/win/udp.c
        src/win/util.c
        src/win/winapi.c
        src/win/winsock.c
    )

    # Windows compiler definitions
    # XXX: Use PRIVATE here so they don't leak into dependent projects
    # Slightly redundant, but less conflicts this way
    list(APPEND UV_DEFINES
        PRIVATE _WIN32_WINNT=0x0600
        PRIVATE _GNU_SOURCE
        PRIVATE _CRT_SECURE_NO_DEPRECATE
        PRIVATE _CRT_NONSTDC_NO_DEPRECATE
    )

    # Find and use windows system libraries
    # These correspond to the FindXXX.cmake file in the cmake folder
    find_package(Advapi32 REQUIRED)
    find_package(Iphlpapi REQUIRED)
    find_package(Psapi REQUIRED)
    find_package(Shell32 REQUIRED)
    find_package(User32 REQUIRED)
    find_package(Userenv REQUIRED)
    find_package(Ws2_32 REQUIRED)
    list(APPEND UV_LIBRARIES
        ${ADVAPI32_LIBRARIES}
        ${IPHLPAPI_LIBRARIES}
        ${PSAPI_LIBRARIES}
        ${SHELL32_LIBRARIES}
        ${USER32_LIBRARIES}
        ${USERENV_LIBRARIES}
        ${WS2_32_LIBRARIES}
    )
# Building for unix
else()
    # Unix private headers
    list(APPEND UV_PRIVATE_HEADERS
        src/unix/atomic-ops.h
        src/unix/spinlock.h
        src/unix/internal.h
    )

    # Unix common sources
    list(APPEND UV_SOURCES
        src/unix/async.c
        src/unix/core.c
        src/unix/dl.c
        src/unix/fs.c
        src/unix/getaddrinfo.c
        src/unix/getnameinfo.c
        src/unix/loop.c
        src/unix/loop-watcher.c
        src/unix/pipe.c
        src/unix/poll.c
        src/unix/process.c
        src/unix/signal.c
        src/unix/stream.c
        src/unix/tcp.c
        src/unix/thread.c
        src/unix/timer.c
        src/unix/tty.c
        src/unix/udp.c
    )

    # Unix common defines
    list(APPEND UV_DEFINES
        PRIVATE _LARGEFILE_SOURCE
        PRIVATE _FILE_OFFSET_BITS=64
    )

    # Unix common libraries
    find_package(Threads REQUIRED)
    find_package(Math REQUIRED)
    list(APPEND UV_INCLUDE PRIVATE ${M_INCLUDE_DIRS})
    list(APPEND UV_LIBRARIES
        ${M_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
    )

    # Proctitle is only supported on these platforms
    if(LINUX OR APPLE OR ANDROID)
        list(APPEND UV_SOURCES src/unix/proctitle.c)
    endif()

    if(APPLE)
        # OSX and IOS support RPATH
        set(CMAKE_SKIP_BUILD_RPATH True)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH True)

        # Apple RPATH should use OSX "variables" for relative locations
        # Debug and Release are explicitly stated here for Xcode configuration
        # build
        list(APPEND CMAKE_INSTALL_RPATH
            @executable_path/../lib/Debug
            @executable_path/../lib/Release
            @executable_path/../lib
            @loader_path/../lib/Debug
            @loader_path/../lib/Release
            @loader_path/../lib
            /usr/local/lib
            /usr/lib
            @executable_path/../Frameworks/Debug
            @executable_path/../Frameworks/Release
            @executable_path/../Frameworks
            @loader_path/../Frameworks/Debug
            @loader_path/../Frameworks/Release
            @loader_path/../Frameworks
            /Library/Frameworks
            /System/Library/Frameworks
        )

        # OSX/IOS sources
        list(APPEND UV_SOURCES
            src/unix/darwin.c
            src/unix/fsevents.c
            src/unix/darwin-proctitle.c
            src/unix/pthread-barrier.c
            src/unix/kqueue.c
        )

        # OSX/IOS defines
        list(APPEND UV_DEFINES
            PUBLIC _DARWIN_USE_64_BIT_INODE=1
            PUBLIC _DARWIN_UNLIMITED_SELECT=1
        )
    elseif(LINUX)
        # Except for android, use RPATH to make the built libraries relocatable
        # TODO: This should be adjusted for other platforms if RPATH isn't 
        # supported
        if(NOT ANDROID)
            set(CMAKE_SKIP_BUILD_RPATH True)
            set(CMAKE_BUILD_WITH_INSTALL_RPATH True)

            # Use the $ORIGIN variable for relative locations
            list(APPEND CMAKE_INSTALL_RPATH $ORIGIN)
            if(CMAKE_LIBRARY_ARCHITECTURE)
                list(APPEND CMAKE_INSTALL_RPATH
                    $ORIGIN/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                    $ORIGIN/../lib/${CMAKE_LIBRARY_ARCHITECTURE}
                )
            endif()
            list(APPEND CMAKE_INSTALL_RPATH
                $ORIGIN/lib
                $ORIGIN/../lib
                /usr/local/lib
                /usr/lib
            )
        endif()

        # Shared linux headers
        list(APPEND UV_PRIVATE_HEADERS
            src/unix/linux-syscalls.h
        )
        # Shared linux sources
        list(APPEND UV_SOURCES
            src/unix/linux-core.c
            src/unix/linux-inotify.c
            src/unix/linux-syscalls.c
        )
        # Android specific sources
        if(ANDROID)
            list(APPEND UV_SOURCES
                src/unix/pthread-fixes.c
                src/unix/android-ifaddrs.c
                src/unix/pthread-barrier.c
            )
        endif()

        # Linux defines
        list(APPEND UV_DEFINES
            PRIVATE _POSIX_C_SOURCE=200112
            PRIVATE _GNU_SOURCE
        )

        # Find and use linux libraries
        # These correspond to the FindXXX.cmake file in the cmake folder
        find_package(DL REQUIRED)
        list(APPEND UV_INCLUDE PRIVATE ${DL_INCLUDES})
        list(APPEND UV_LIBRARIES ${DL_LIBRARIES})
        if(NOT ANDROID)
            find_package(RT REQUIRED)
            list(APPEND UV_LIBRARIES ${RT_LIBRARIES})
        endif()
    elseif(SOLARIS)
        # Solaris specific configuration
        list(APPEND UV_SOURCES src/unix/sunos.c)
        list(APPEND UV_DEFINES
            PRIVATE __EXTENSIONS__
            PRIVATE _XOPEN_SOURCE=500
        )

        # Find and use solaris libraries
        # These correspond to the FindXXX.cmake file in the cmake folder
        find_package(Kstat REQUIRED)
        find_package(Nsl REQUIRED)
        find_package(Sendfile REQUIRED)
        find_package(Socket REQUIRED)
        list(APPEND UV_LIBRARIES
            ${KSTAT_LIBRARIES}
            ${NSL_LIBRARIES}
            ${SENDFILE_LIBRARIES}
            ${SOCKET_LIBRARIES}
        )
    elseif(AIX)
        # AIX specific configuration
        list(APPEND UV_SOURCES src/unix/aix.c)
        list(APPEND UV_DEFINES
            PRIVATE _ALL_SOURCE
            PRIVATE _XOPEN_SOURCE=500
            PRIVATE _LINUX_SOURCE_COMPAT
            PRIVATE _THREAD_SAFE
        )

        # Find and use AIX libraries
        # These correspond to the FindXXX.cmake file in the cmake folder
        find_package(Perfstat REQUIRED)
        list(APPEND UV_LIBRARIES ${PERFSTAT_LIBRARIES})
    elseif(BSD)
        # BSD specific configuration
        if(FREEBSD OR DRAGONFLYBSD)
            list(APPEND UV_SOURCES src/unix/freebsd.c)
        elseif(OPENBSD)
            list(APPEND UV_SOURCES src/unix/openbsd.c)
        elseif(NETBSD)
            list(APPEND UV_SOURCES src/unix/netbsd.c)
        endif()

        # BSD shared sources (duplicated for OSX, which is technically BSD)
        list(APPEND UV_SOURCES src/unix/kqueue.c)

        # Find and use BSD libraries
        # These correspond to the FindXXX.cmake file in the cmake folder
        find_package(Kvm REQUIRED)
        list(APPEND UV_LIBRARIES ${KVM_LIBRARIES})
    endif()
endif()

# Install public headers to the output tree
file(INSTALL ${UV_HEADERS} DESTINATION ${CMAKE_INCLUDE_OUTPUT_DIRECTORY})

# Generate a shared library
if(UV_BUILD_SHARED)
    add_library(${PROJECT_NAME}-shared SHARED 
        ${UV_HEADERS}
        ${UV_PRIVATE_HEADERS}
        ${UV_SOURCES}
    )
    # USING_UV_SHARED here DEPENDS on the ordering of defines in uv.h
    target_compile_definitions(${PROJECT_NAME}-shared 
        ${UV_DEFINES}
        PUBLIC USING_UV_SHARED=1
        PRIVATE BUILDING_UV_SHARED=1
    )
    target_include_directories(${PROJECT_NAME}-shared ${UV_INCLUDE})
    target_link_libraries(${PROJECT_NAME}-shared ${UV_LIBRARIES})
    set_target_properties(${PROJECT_NAME}-shared PROPERTIES
        OUTPUT_NAME ${UV_LIBRARY_NAME}
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO 
            ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        DEBUG_POSTFIX _d
        VERSION ${UV_VERSION}
        SOVERSION ${UV_VERSION_SOVER}
        POSITION_INDEPENDENT_CODE True
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
    )
    list(APPEND UV_EXPORT_TARGETS ${PROJECT_NAME}-shared)
    if(NOT UV_BUILT_LIBRARY)
        set(UV_BUILT_LIBRARY ${PROJECT_NAME}-shared)
    endif()
endif()

# Generate an OSX framework
if(UV_BUILD_FRAMEWORK)
    add_library(${PROJECT_NAME}-framework SHARED
        ${UV_HEADERS}
        ${UV_PRIVATE_HEADERS}
        ${UV_SOURCES}
    )
    target_compile_definitions(${PROJECT_NAME}-framework
        ${UV_DEFINES}
        PUBLIC USING_UV_SHARED=1
        PRIVATE BUILDING_UV_SHARED=1
    )
    target_include_directories(${PROJECT_NAME}-framework ${UV_INCLUDE})
    target_link_libraries(${PROJECT_NAME}-framework ${UV_LIBRARIES})
    set_target_properties(${PROJECT_NAME}-framework PROPERTIES
        OUTPUT_NAME lib${UV_LIBRARY_NAME}$<$<CONFIG:Debug>:_d>
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO 
            ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        VERSION ${UV_VERSION}
        SOVERSION ${UV_VERSION_SOVER}
        POSITION_INDEPENDENT_CODE True
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        FRAMEWORK True
        FRAMEWORK_VERSION ${UV_VERSION}
        PUBLIC_HEADER "${UV_HEADERS}"
    )
    list(APPEND UV_EXPORT_TARGETS_LOCAL ${PROJECT_NAME}-framework)
endif()

# Generate a static library
if(UV_BUILD_STATIC)
    add_library(${PROJECT_NAME}-static STATIC
        ${UV_HEADERS}
        ${UV_PRIVATE_HEADERS}
        ${UV_SOURCES}
    )
    target_compile_definitions(${PROJECT_NAME}-static ${UV_DEFINES})
    target_include_directories(${PROJECT_NAME}-static ${UV_INCLUDE})
    target_link_libraries(${PROJECT_NAME}-static ${UV_LIBRARIES})
    set_target_properties(${PROJECT_NAME}-static PROPERTIES
        OUTPUT_NAME ${UV_LIBRARY_NAME}
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO 
            ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
        DEBUG_POSTFIX _d
    )
    list(APPEND UV_EXPORT_TARGETS ${PROJECT_NAME}-static)
    if(NOT UV_BUILT_LIBRARY)
        set(UV_BUILT_LIBRARY ${PROJECT_NAME}-static)
    endif()
endif()

# Include installed export targets in local targets
list(APPEND UV_EXPORT_TARGETS_LOCAL ${UV_EXPORT_TARGETS})

# Install uv libraries and headers
install(TARGETS ${UV_EXPORT_TARGETS}
    EXPORT ${PROJECT_NAME}
    LIBRARY DESTINATION ${UV_LIBRARY_INSTALL_PATH}
    ARCHIVE DESTINATION ${UV_LIBRARY_INSTALL_PATH}
)
install(FILES ${UV_HEADERS} DESTINATION ${UV_INCLUDE_INSTALL_PATH})

# Generate in-tree package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_PACKAGE_OUTPUT_DIRECTORY}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${UV_VERSION}
    COMPATIBILITY SameMajorVersion
)
export(TARGETS ${UV_EXPORT_TARGETS_LOCAL}
    FILE ${CMAKE_PACKAGE_OUTPUT_DIRECTORY}/${PROJECT_NAME}Config.cmake
)

# Generate installed package configuration
install(
    FILES ${CMAKE_PACKAGE_OUTPUT_DIRECTORY}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION ${UV_PACKAGE_INSTALL_PATH}
)
install(EXPORT ${PROJECT_NAME} 
    DESTINATION ${UV_PACKAGE_INSTALL_PATH}
    FILE ${PROJECT_NAME}Config.cmake
)

# Provide an uninstall target
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE @ONLY
)
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND}
    ARGS -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)

# Configure testing
if(UV_TESTS_ENABLED)
    if(NOT UV_BUILT_LIBRARY)
        message(FATAL_ERROR "No library was built, tests cannot be run")
    endif()

    # Enable CTest
    enable_testing()

    # Shared test headers
    list(APPEND UV_TEST_HEADERS
        test/runner.h
        test/task.h
        test/test-list.h
    )
    # Shared test sources
    list(APPEND UV_TEST_SOURCES
        test/blackhole-server.c
        test/echo-server.c
        test/dns-server.c
        test/runner.c
    )

    # Unit test sources
    list(APPEND UV_UNIT_TEST_SOURCES
        test/test-get-loadavg.c
        test/test-active.c
        test/test-async.c
        test/test-async-null-cb.c
        test/test-callback-stack.c
        test/test-callback-order.c
        test/test-close-fd.c
        test/test-close-order.c
        test/test-connection-fail.c
        test/test-cwd-and-chdir.c
        test/test-default-loop-close.c
        test/test-delayed-accept.c
        test/test-eintr-handling.c
        test/test-error.c
        test/test-embed.c
        test/test-emfile.c
        test/test-fail-always.c
        test/test-fs.c
        test/test-fs-event.c
        test/test-get-currentexe.c
        test/test-get-memory.c
        test/test-get-passwd.c
        test/test-getaddrinfo.c
        test/test-getnameinfo.c
        test/test-getsockname.c
        test/test-handle-fileno.c
        test/test-homedir.c
        test/test-hrtime.c
        test/test-idle.c
        test/test-ip6-addr.c
        test/test-ipc.c
        test/test-ipc-send-recv.c
        test/test-loop-handles.c
        test/test-loop-alive.c
        test/test-loop-close.c
        test/test-loop-stop.c
        test/test-loop-time.c
        test/test-loop-configure.c
        test/test-walk-handles.c
        test/test-watcher-cross-stop.c
        test/test-multiple-listen.c
        test/test-osx-select.c
        test/test-pass-always.c
        test/test-ping-pong.c
        test/test-pipe-bind-error.c
        test/test-pipe-connect-error.c
        test/test-pipe-connect-multiple.c
        test/test-pipe-connect-prepare.c
        test/test-pipe-getsockname.c
        test/test-pipe-pending-instances.c
        test/test-pipe-sendmsg.c
        test/test-pipe-server-close.c
        test/test-pipe-close-stdout-read-stdin.c
        test/test-pipe-set-non-blocking.c
        test/test-platform-output.c
        test/test-poll.c
        test/test-poll-close.c
        test/test-poll-close-doesnt-corrupt-stack.c
        test/test-poll-closesocket.c
        test/test-process-title.c
        test/test-queue-foreach-delete.c
        test/test-ref.c
        test/test-run-nowait.c
        test/test-run-once.c
        test/test-semaphore.c
        test/test-shutdown-close.c
        test/test-shutdown-eof.c
        test/test-shutdown-twice.c
        test/test-signal.c
        test/test-signal-multiple-loops.c
        test/test-socket-buffer-size.c
        test/test-spawn.c
        test/test-fs-poll.c
        test/test-stdio-over-pipes.c
        test/test-tcp-bind-error.c
        test/test-tcp-bind6-error.c
        test/test-tcp-close.c
        test/test-tcp-close-accept.c
        test/test-tcp-close-while-connecting.c
        test/test-tcp-create-socket-early.c
        test/test-tcp-connect-error-after-write.c
        test/test-tcp-shutdown-after-write.c
        test/test-tcp-flags.c
        test/test-tcp-connect-error.c
        test/test-tcp-connect-timeout.c
        test/test-tcp-connect6-error.c
        test/test-tcp-open.c
        test/test-tcp-write-to-half-open-connection.c
        test/test-tcp-write-after-connect.c
        test/test-tcp-writealot.c
        test/test-tcp-write-fail.c
        test/test-tcp-try-write.c
        test/test-tcp-unexpected-read.c
        test/test-tcp-oob.c
        test/test-tcp-read-stop.c
        test/test-tcp-write-queue-order.c
        test/test-threadpool.c
        test/test-threadpool-cancel.c
        test/test-thread-equal.c
        test/test-tmpdir.c
        test/test-mutexes.c
        test/test-thread.c
        test/test-barrier.c
        test/test-condvar.c
        test/test-timer-again.c
        test/test-timer-from-check.c
        test/test-timer.c
        test/test-tty.c
        test/test-udp-bind.c
        test/test-udp-create-socket-early.c
        test/test-udp-dgram-too-big.c
        test/test-udp-ipv6.c
        test/test-udp-open.c
        test/test-udp-options.c
        test/test-udp-send-and-recv.c
        test/test-udp-send-immediate.c
        test/test-udp-send-unreachable.c
        test/test-udp-multicast-join.c
        test/test-udp-multicast-join6.c
        test/test-dlerror.c
        test/test-udp-multicast-ttl.c
        test/test-ip4-addr.c
        test/test-ip6-addr.c
        test/test-udp-multicast-interface.c
        test/test-udp-multicast-interface6.c
        test/test-udp-try-send.c
        test/run-tests.c
    )

    # Benchmark sources
    list(APPEND UV_BENCHMARK_TEST_SOURCES
        test/benchmark-async.c
        test/benchmark-async-pummel.c
        test/benchmark-fs-stat.c
        test/benchmark-getaddrinfo.c
        test/benchmark-list.h
        test/benchmark-loop-count.c
        test/benchmark-million-async.c
        test/benchmark-million-timers.c
        test/benchmark-multi-accept.c
        test/benchmark-ping-pongs.c
        test/benchmark-pound.c
        test/benchmark-pump.c
        test/benchmark-sizes.c
        test/benchmark-spawn.c
        test/benchmark-thread.c
        test/benchmark-tcp-write-batch.c
        test/benchmark-udp-pummel.c
        test/run-benchmarks.c
    )

    # Shared test configuration
    list(APPEND UV_TEST_LIBRARIES ${UV_BUILT_LIBRARY})
    list(APPEND UV_TEST_INCLUDE 
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test
    )

    if(WIN32)
        # Windows specific test files
        list(APPEND UV_TEST_SOURCES
            test/runner-win.c
            test/runner-win.h
            src/win/snprintf.c
        )
    else()
        # Unix specific test files and configuration
        list(APPEND UV_TEST_SOURCES
            test/runner-unix.c
            test/runner-unix.h
        )
        list(APPEND UV_TEST_DEFINES PRIVATE _GNU_SOURCE)
        if(SOLARIS)
            list(APPEND UV_TEST_DEFINES 
                PRIVATE __EXTENSIONS__
                PRIVATE _XOPEN_SOURCE=500
            )
        elseif(AIX)
            list(APPEND UV_TEST_DEFINES
                PRIVATE _ALL_SOURCE
                PRIVATE _XOPEN_SOURCE=500
            )
        endif()
        if(APPLE OR BSD)
            find_package(Util REQUIRED)
            list(APPEND UV_TEST_LIBRARIES ${UTIL_LIBRARIES})
        endif()
    endif()

    # Unit test executable
    add_executable(run-tests 
        ${UV_TEST_HEADERS} 
        ${UV_TEST_SOURCES} 
        ${UV_UNIT_TEST_SOURCES}
    )
    if(UV_TEST_DEFINES)
        target_compile_definitions(run-tests ${UV_TEST_DEFINES})
    endif()
    target_include_directories(run-tests ${UV_TEST_INCLUDE})
    target_link_libraries(run-tests ${UV_TEST_LIBRARIES})
    set_target_properties(run-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        POSITION_INDEPENDENT_CODE True
    )

    # Benchmark executable
    add_executable(run-benchmarks 
        ${UV_TEST_HEADERS} 
        ${UV_TEST_SOURCES}
        ${UV_BENCHMARK_TEST_SOURCES}
    )
    target_compile_definitions(run-benchmarks ${UV_TEST_DEFINES})
    target_include_directories(run-benchmarks ${UV_TEST_INCLUDE})
    target_link_libraries(run-benchmarks ${UV_TEST_LIBRARIES})
    set_target_properties(run-benchmarks PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        POSITION_INDEPENDENT_CODE True
    )

    # Add test executables to CTest
    add_test(NAME run-tests
        COMMAND run-tests 
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME run-benchmarks 
        COMMAND run-benchmarks
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()