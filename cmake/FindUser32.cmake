find_library(USER32_LIBRARY user32)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(USER32 DEFAULT_MSG USER32_LIBRARY)

if(USER32_FOUND)
    set(USER32_LIBRARIES ${USER32_LIBRARY})
endif()

mark_as_advanced(USER32_LIBRARIES)