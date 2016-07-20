find_path(DL_INCLUDE_DIR dlfcn.h)

find_library(DL_LIBRARY NAMES dl)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DL DEFAULT_MESSAGE DL_LIBRARY DL_INCLUDE_DIR)

if(DL_FOUND)
    set(DL_LIBRARIES ${DL_LIBRARY})
    set(DL_INCLUDE_DIRS ${DL_INCLUDE_DIRS})
endif()

mark_as_advanced(DL_LIBRARIES DL_INCLUDE_DIRS)