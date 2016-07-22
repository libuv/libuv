find_library(SENDFILE_LIBRARY NAMES sendfile)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SENDFILE DEFAULT_MSG SENDFILE_LIBRARY)

if(SENDFILE_FOUND)
    set(SENDFILE_LIBRARIES ${SENDFILE_LIBRARY})
endif()

mark_as_advanced(SENDFILE_LIBRARIES)