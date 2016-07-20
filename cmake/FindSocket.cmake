find_library(SOCKET_LIBRARY NAMES socket)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SOCKET DEFAULT_MESSAGE SOCKET_LIBRARY)

if(SOCKET_FOUND)
    set(SOCKET_LIBRARIES ${SOCKET_LIBRARY})
endif()

mark_as_advanced(SOCKET_LIBRARIES)