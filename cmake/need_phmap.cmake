#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# dwarfs is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# dwarfs is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# dwarfs.  If not, see <https://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.28.0)

try_run(
  PHMAP_RUN_RESULT
  PHMAP_COMPILE_RESULT
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/phmap-version.cpp
  CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${TRY_RUN_INCLUDE_DIRECTORIES}
  CXX_STANDARD ${DWARFS_CXX_STANDARD}
  RUN_OUTPUT_VARIABLE PHMAP_VERSION
  COMPILE_OUTPUT_VARIABLE PHMAP_COMPILE_OUTPUT
)

if(PHMAP_RUN_RESULT EQUAL 0)
  if(PHMAP_VERSION VERSION_LESS ${PARALLEL_HASHMAP_REQUIRED_VERSION})
    string(STRIP "${PHMAP_VERSION}" PHMAP_VERSION)
    message(STATUS "System-installed parallel-hashmap version ${PHMAP_VERSION} is less than required version ${PARALLEL_HASHMAP_REQUIRED_VERSION}")
  endif()
else()
  message(STATUS "failed to check parallel-hashmap version")
  message(VERBOSE "${PHMAP_COMPILE_OUTPUT}")
endif()

if(PHMAP_RUN_RESULT EQUAL 0 AND PHMAP_VERSION VERSION_GREATER_EQUAL ${PARALLEL_HASHMAP_REQUIRED_VERSION})
  add_library(phmap INTERFACE)
else()
  FetchContent_Declare(
    parallel-hashmap
    GIT_REPOSITORY ${PARALLEL_HASHMAP_GIT_REPO}
    GIT_TAG v${PARALLEL_HASHMAP_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(parallel-hashmap)
endif()
