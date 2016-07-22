find_library(SHELL32_LIBRARY shell32)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SHELL32 DEFAULT_MSG SHELL32_LIBRARY)

if(SHELL32_FOUND)
    set(SHELL32_LIBRARIES ${SHELL32_LIBRARY})
endif()

mark_as_advanced(SHELL32_LIBRARIES)