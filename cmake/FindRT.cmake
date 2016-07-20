find_library(RT_LIBRARY NAMES rt)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RT DEFAULT_MESSAGE RT_LIBRARY)

if(RT_FOUND)
    set(RT_LIBRARIES ${RT_LIBRARY})
endif()

mark_as_advanced(RT_LIBRARIES)