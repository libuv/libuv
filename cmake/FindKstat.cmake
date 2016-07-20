find_library(KSTAT_LIBRARY NAMES kstat)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(KSTAT DEFAULT_MESSAGE KSTAT_LIBRARY)

if(KSTAT_FOUND)
    set(KSTAT_LIBRARIES ${KSTAT_LIBRARY})
endif()

mark_as_advanced(KSTAT_LIBRARIES)