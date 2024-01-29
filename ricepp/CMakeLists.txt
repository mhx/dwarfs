#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of ricepp.
#
# ricepp is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# ricepp is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# ricepp.  If not, see <https://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.25.0)

project(ricepp)

include(FetchContent)

if(NOT TARGET range-v3)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY https://github.com/ericniebler/range-v3
    GIT_TAG 0.12.0
  )
  FetchContent_MakeAvailable(range-v3)
endif()

add_library(ricepp ricepp.cpp)

target_include_directories(ricepp PUBLIC include)
target_link_libraries(ricepp PUBLIC range-v3)
target_compile_features(ricepp PUBLIC cxx_std_20)

# # TODO: remove/rework
# add_executable(ricepp_demo ricepp_demo.cpp)
# target_link_libraries(ricepp_demo PRIVATE ricepp fmt)

if(WITH_TESTS)
  if(NOT TARGET gtest)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  include(GoogleTest)

  add_executable(ricepp_test
    test/bitstream_test.cpp
    test/byteorder_test.cpp
    test/codec_test.cpp
  )

  target_link_libraries(ricepp_test PRIVATE ricepp gtest gmock gtest_main)

  if(ENABLE_COVERAGE)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
      foreach(target ricepp ricepp_test)
        target_compile_options(
          ${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping
                            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fprofile-instr-generate
                                              -fcoverage-mapping)
      endforeach()
    endif()
  endif()

  if(NOT CMAKE_CROSSCOMPILING)
    gtest_discover_tests(ricepp_test DISCOVERY_TIMEOUT 120)
  endif()
endif()
