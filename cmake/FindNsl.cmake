find_library(NSL_LIBRARY NAMES nsl)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NSL DEFAULT_MESSAGE NSL_LIBRARY)

if(NSL_FOUND)
    set(NSL_LIBRARIES ${NSL_LIBRARY})
endif()

mark_as_advanced(NSL_LIBRARIES)