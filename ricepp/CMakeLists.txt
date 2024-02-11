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

if(WIN32)
  add_compile_options(/Zc:__cplusplus /utf-8 /wd4267 /wd4244 /wd5219)
  # Apply /MT or /MTd  (multithread, static version of the run-time library)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
  add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

CHECK_CXX_COMPILER_FLAG(-mbmi2 COMPILER_SUPPORTS_MBMI2)
CHECK_CXX_COMPILER_FLAG(-mavx512vl COMPILER_SUPPORTS_MAVX512VL)
CHECK_CXX_COMPILER_FLAG(-mavx512vbmi COMPILER_SUPPORTS_MAVX512VBMI)

add_library(ricepp_fallback ricepp_cpuspecific.cpp)
target_compile_definitions(ricepp_fallback PRIVATE RICEPP_CPU_VARIANT=fallback)
list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_fallback)

if(COMPILER_SUPPORTS_MBMI2)
  add_library(ricepp_bmi2 ricepp_cpuspecific.cpp)
  target_compile_options(ricepp_bmi2 PRIVATE -mbmi2)
  target_compile_definitions(ricepp_bmi2 PRIVATE RICEPP_CPU_VARIANT=has_bmi2)
  list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_bmi2)

  if(COMPILER_SUPPORTS_MAVX512VL AND COMPILER_SUPPORTS_MAVX512VBMI)
    add_library(ricepp_bmi2_avx512 ricepp_cpuspecific.cpp)
    target_compile_options(ricepp_bmi2_avx512 PRIVATE -mbmi2 -mavx512vl -mavx512vbmi)
    target_compile_definitions(ricepp_bmi2_avx512 PRIVATE RICEPP_CPU_VARIANT=has_bmi2_avx512)
    list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_bmi2_avx512)
  endif()
endif()

foreach(target ${RICEPP_LIBS_CPUSPECIFIC})
  message(STATUS "Adding target: ${target}")
  target_include_directories(${target} PUBLIC include)
  target_link_libraries(${target} PUBLIC range-v3)
  target_compile_features(${target} PUBLIC cxx_std_20)
endforeach()

add_library(ricepp ricepp.cpp)
target_link_libraries(ricepp PRIVATE ${RICEPP_LIBS_CPUSPECIFIC})
target_include_directories(ricepp PUBLIC include)
target_compile_features(ricepp PUBLIC cxx_std_20)

# # TODO: remove/rework
# add_executable(ricepp_demo ricepp_demo.cpp)
# target_link_libraries(ricepp_demo PRIVATE ricepp fmt)

if(WITH_BENCHMARKS)
  find_package(benchmark)
  if(benchmark_FOUND)
    add_executable(ricepp_benchmark ricepp_benchmark.cpp)
    target_link_libraries(ricepp_benchmark ricepp benchmark::benchmark)
  endif()
endif()

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
