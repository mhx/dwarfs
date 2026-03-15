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

if(PREFER_SYSTEM_GTEST)
  find_package(GTest ${GOOGLETEST_REQUIRED_VERSION} CONFIG)
  add_library(gtest ALIAS GTest::gtest)
  add_library(gtest_main ALIAS GTest::gtest_main)
  add_library(gmock ALIAS GTest::gmock)
  add_library(gmock_main ALIAS GTest::gmock_main)

  try_compile(
    GTEST_SUPPORTS_U8STRING
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/gtest-u8string.cpp
  )

  if(NOT GTEST_SUPPORTS_U8STRING)
    message(WARNING "GTest does not support u8string.")
    target_compile_definitions(GTest::gtest INTERFACE GTEST_NO_U8STRING=1)
  endif()
else()
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY ${GOOGLETEST_GIT_REPO}
    GIT_TAG v${GOOGLETEST_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)
