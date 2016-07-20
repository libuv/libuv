set(CMAKE_FIND_FRAMEWORK LAST)
find_path(MATH_INCLUDE_DIR math.h)

find_library(MATH_LIBRARY NAMES m)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MATH 
    DEFAULT_MESSAGE MATH_LIBRARY MATH_INCLUDE_DIR
)

if(MATH_FOUND)
    set(MATH_LIBRARIES ${MATH_LIBRARY})
    set(MATH_INCLUDE_DIRS ${MATH_INCLUDE_DIRS})
endif()

mark_as_advanced(MATH_LIBRARIES MATH_INCLUDE_DIRS)