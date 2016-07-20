find_library(WS2_32_LIBRARY ws2_32)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WS2_32 DEFAULT_MESSAGE WS2_32_LIBRARY)

if(WS2_32_FOUND)
    set(WS2_32_LIBRARIES ${WS2_32_LIBRARY})
endif()

mark_as_advanced(WS2_32_LIBRARIES)