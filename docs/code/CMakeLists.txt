cmake_minimum_required(VERSION 3.5)

project(libuv_sample)

add_subdirectory("../../" build)

set(SIMPLE_SAMPLES
        cgi
        helloworld
        dns
        detach
        default-loop
        idle-basic
        idle-compute
        interfaces
        locks
        onchange
        pipe-echo-server
        progress
        queue-cancel
        queue-work
        ref-timer
        signal
        spawn
        tcp-echo-server
        thread-create
        tty
        tty-gravity
        udp-dhcp
        uvcat
        uvstop
        uvtee
        )

foreach (X IN LISTS SIMPLE_SAMPLES)
    add_executable(${X} ${X}/main.c)
    target_link_libraries(${X} uv_a)
endforeach ()


FIND_PACKAGE(CURL)
IF(CURL_FOUND)
    add_executable(uvwget uvwget/main.c)
    target_link_libraries(uvwget uv_a ${CURL_LIBRARIES})
ENDIF(CURL_FOUND)
