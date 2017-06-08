find_library(USERENV_LIBRARY userenv)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(USERENV DEFAULT_MSG USERENV_LIBRARY)

if(USERENV_FOUND)
    set(USERENV_LIBRARIES ${USERENV_LIBRARY})
endif()

mark_as_advanced(USERENV_LIBRARIES)