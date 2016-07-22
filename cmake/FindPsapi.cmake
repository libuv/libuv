find_library(PSAPI_LIBRARY psapi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PSAPI DEFAULT_MSG PSAPI_LIBRARY)

if(PSAPI_FOUND)
    set(PSAPI_LIBRARIES ${PSAPI_LIBRARY})
endif()

mark_as_advanced(PSAPI_LIBRARIES)