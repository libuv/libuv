#[=======================================================================[

FindLibuv
---------

Find libuv includes and library.

Imported Targets
^^^^^^^^^^^^^^^^

An :ref:`imported target <Imported targets>` named
``LIBUV::LIBUV`` is provided if libuv has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

``LIBUV_FOUND``
  True if libuv was found, false otherwise.
``LIBUV_INCLUDE_DIRS``
  Include directories needed to include libuv headers.
``LIBUV_LIBRARIES``
  Libraries needed to link to libuv.
``LIBUV_VERSION``
  The version of libuv found.
``LIBUV_VERSION_MAJOR``
  The major version of libuv.
``LIBUV_VERSION_MINOR``
  The minor version of libuv.
``LIBUV_VERSION_PATCH``
  The patch version of libuv.

Cache Variables
^^^^^^^^^^^^^^^

This module uses the following cache variables:

``LIBUV_LIBRARY``
  The location of the libuv library file.
``LIBUV_INCLUDE_DIR``
  The location of the libuv include directory containing ``uv.h``.

The cache variables should not be used by project code.
They may be set by end users to point at libuv components.
#]=======================================================================]

#=============================================================================
# Copyright 2014-2016 Kitware, Inc.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

#-----------------------------------------------------------------------------
find_library(libuv_LIBRARY
  NAMES uv
  )
mark_as_advanced(libuv_LIBRARY)

find_path(libuv_INCLUDE_DIR
  NAMES uv.h
  )
mark_as_advanced(libuv_INCLUDE_DIR)

#-----------------------------------------------------------------------------
# Extract version number if possible.
set(_LIBUV_H_REGEX "#[ \t]*define[ \t]+UV_VERSION_(MAJOR|MINOR|PATCH)[ \t]+[0-9]+")
if(LIBUV_INCLUDE_DIR AND EXISTS "${LIBUV_INCLUDE_DIR}/uv-version.h")
  file(STRINGS "${LIBUV_INCLUDE_DIR}/uv-version.h" _LIBUV_H REGEX "${_LIBUV_H_REGEX}")
elseif(LIBUV_INCLUDE_DIR AND EXISTS "${LIBUV_INCLUDE_DIR}/uv.h")
  file(STRINGS "${LIBUV_INCLUDE_DIR}/uv.h" _LIBUV_H REGEX "${_LIBUV_H_REGEX}")
else()
  set(_LIBUV_H "")
endif()
foreach(c MAJOR MINOR PATCH)
  if(_LIBUV_H MATCHES "#[ \t]*define[ \t]+UV_VERSION_${c}[ \t]+([0-9]+)")
    set(_LIBUV_VERSION_${c} "${CMAKE_MATCH_1}")
  else()
    unset(_LIBUV_VERSION_${c})
  endif()
endforeach()

if(DEFINED _LIBUV_VERSION_MAJOR AND DEFINED _LIBUV_VERSION_MINOR)
  set(LIBUV_VERSION_MAJOR "${_LIBUV_VERSION_MAJOR}")
  set(LIBUV_VERSION_MINOR "${_LIBUV_VERSION_MINOR}")
  set(LIBUV_VERSION "${LIBUV_VERSION_MAJOR}.${LIBUV_VERSION_MINOR}")
  if(DEFINED _LIBUV_VERSION_PATCH)
    set(LIBUV_VERSION_PATCH "${_LIBUV_VERSION_PATCH}")
    set(LIBUV_VERSION "${LIBUV_VERSION}.${LIBUV_VERSION_PATCH}")
  else()
    unset(LIBUV_VERSION_PATCH)
  endif()
else()
  set(LIBUV_VERSION_MAJOR "")
  set(LIBUV_VERSION_MINOR "")
  set(LIBUV_VERSION_PATCH "")
  set(LIBUV_VERSION "")
endif()
unset(_LIBUV_VERSION_MAJOR)
unset(_LIBUV_VERSION_MINOR)
unset(_LIBUV_VERSION_PATCH)
unset(_LIBUV_H_REGEX)
unset(_LIBUV_H)

#-----------------------------------------------------------------------------
# Set Find Package Arguments
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args(libuv
    FOUND_VAR libuv_FOUND
    REQUIRED_VARS LIBUV_LIBRARY LIBUV_INCLUDE_DIR
    VERSION_VAR LIBUV_VERSION
    HANDLE_COMPONENTS
        FAIL_MESSAGE
        "Could NOT find Libuv"
)

set(LIBUV_FOUND ${libuv_FOUND})

#-----------------------------------------------------------------------------
# Provide documented result variables and targets.
if(LIBUV_FOUND)
  set(LIBUV_INCLUDE_DIRS ${LIBUV_INCLUDE_DIR})
  set(LIBUV_LIBRARIES ${LIBUV_LIBRARY})
  if(NOT TARGET LIBUV::LIBUV)
    add_library(LIBUV::LIBUV UNKNOWN IMPORTED)
    set_target_properties(LIBUV::LIBUV PROPERTIES
      IMPORTED_LOCATION "${LIBUV_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBUV_INCLUDE_DIRS}"
      )
  endif()
endif()
