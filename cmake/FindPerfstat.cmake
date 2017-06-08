find_library(PERFSTAT_LIBRARY NAMES perfstat)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PERFSTAT DEFAULT_MSG PERFSTAT_LIBRARY)

if(PERFSTAT_FOUND)
    set(PERFSTAT_LIBRARIES ${PERFSTAT_LIBRARY})
endif()

mark_as_advanced(PERFSTAT_LIBRARIES)