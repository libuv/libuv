find_library(ADVAPI32_LIBRARY advapi32)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ADVAPI32 DEFAULT_MESSAGE ADVAPI32_LIBRARY)

if(ADVAPI32_FOUND)
    set(ADVAPI32_LIBRARIES ${ADVAPI32_LIBRARY})
endif()

mark_as_advanced(ADVAPI32_LIBRARIES)