find_library(UTIL_LIBRARY NAMES util)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UTIL DEFAULT_MSG UTIL_LIBRARY)

if(UTIL_FOUND)
    set(UTIL_LIBRARIES ${UTIL_LIBRARY})
endif()

mark_as_advanced(UTIL_LIBRARIES)