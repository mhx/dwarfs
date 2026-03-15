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

cmake_minimum_required(VERSION 3.28.0)

try_run(
  PHMAP_RUN_RESULT
  PHMAP_COMPILE_RESULT
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/phmap-version.cpp
  CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${TRY_RUN_INCLUDE_DIRECTORIES}
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
