#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT
#

add_library(dwarfs_blake3 INTERFACE)

set(DWARFS_BLAKE3_SYSTEM_CMAKE_CONFIG FALSE)
set(DWARFS_BLAKE3_SYSTEM_PKGCONFIG FALSE)

if(NOT FORCE_BUNDLED_BLAKE3)
  find_package(blake3 ${BLAKE3_REQUIRED_VERSION} QUIET)
endif()

if(blake3_FOUND)
  set(DWARFS_BLAKE3_SYSTEM_CMAKE_CONFIG TRUE)
  set(DWARFS_BLAKE3_PROVIDER "system (CMake package ${blake3_VERSION})")
  target_link_libraries(dwarfs_blake3 INTERFACE BLAKE3::blake3)
else()
  if(NOT FORCE_BUNDLED_BLAKE3)
    pkg_check_modules(BLAKE3 QUIET IMPORTED_TARGET libblake3>=${BLAKE3_REQUIRED_VERSION})
  endif()

  if(BLAKE3_FOUND)
    set(DWARFS_BLAKE3_SYSTEM_PKGCONFIG TRUE)
    set(DWARFS_BLAKE3_PROVIDER "system (pkg-config ${BLAKE3_VERSION})")
    target_link_libraries(dwarfs_blake3 INTERFACE PkgConfig::BLAKE3)
  else()
    set(DWARFS_BLAKE3_VENDORED TRUE)
    set(DWARFS_BLAKE3_PROVIDER "vendored")

    block()
      set(BUILD_SHARED_LIBS OFF)          # don't produce a .so you'd never install
      set(BLAKE3_USE_TBB OFF)
      set(BLAKE3_EXAMPLES OFF)
      add_subdirectory(BLAKE3/c EXCLUDE_FROM_ALL)
    endblock()

    set_target_properties(blake3 PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden
    )

    set(DWARFS_BLAKE3_OBJECTS $<TARGET_OBJECTS:blake3>)

    target_include_directories(dwarfs_blake3 SYSTEM INTERFACE
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/BLAKE3/c>
    )
  endif()
endif()

message(STATUS "BLAKE3: ${DWARFS_BLAKE3_PROVIDER}")
