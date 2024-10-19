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

if(PREFER_SYSTEM_GTEST)
  find_package(GTest ${GOOGLETEST_REQUIRED_VERSION} CONFIG)
  add_library(gtest ALIAS GTest::gtest)
  add_library(gtest_main ALIAS GTest::gtest_main)
  add_library(gmock ALIAS GTest::gmock)
  add_library(gmock_main ALIAS GTest::gmock_main)

  try_compile(
    GTEST_SUPPORTS_U8STRING
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/gtest-u8string.cpp
    CXX_STANDARD 20
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
