find_library(IPHLPAPI_LIBRARY iphlpapi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IPHLPAPI DEFAULT_MSG IPHLPAPI_LIBRARY)

if(IPHLPAPI_FOUND)
    set(IPHLPAPI_LIBRARIES ${IPHLPAPI_LIBRARY})
endif()

mark_as_advanced(IPHLPAPI_LIBRARIES)
