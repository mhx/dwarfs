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

cmake_minimum_required(VERSION 3.28.0)

project(ricepp)

include(FetchContent)

if(NOT TARGET range-v3::range-v3)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY https://github.com/ericniebler/range-v3
    GIT_TAG 0.12.0
    EXCLUDE_FROM_ALL
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

add_library(ricepp_fallback OBJECT ricepp_cpuspecific.cpp)
target_compile_definitions(ricepp_fallback PRIVATE RICEPP_CPU_VARIANT=fallback)
list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_fallback)

if((NOT (WIN32 OR CMAKE_CXX_FLAGS MATCHES "-march=")) AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  CHECK_CXX_COMPILER_FLAG(-mbmi2 COMPILER_SUPPORTS_MBMI2)
  CHECK_CXX_COMPILER_FLAG(-mavx512vl COMPILER_SUPPORTS_MAVX512VL)
  CHECK_CXX_COMPILER_FLAG(-mavx512vbmi COMPILER_SUPPORTS_MAVX512VBMI)

  if(COMPILER_SUPPORTS_MBMI2)
    add_library(ricepp_bmi2 OBJECT ricepp_cpuspecific.cpp)
    target_compile_options(ricepp_bmi2 PRIVATE -mbmi2)
    target_compile_definitions(ricepp_bmi2 PRIVATE RICEPP_CPU_VARIANT=has_bmi2)
    list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_bmi2)
    list(APPEND RICEPP_CPU_SUPPORT RICEPP_CPU_BMI2)

    if(COMPILER_SUPPORTS_MAVX512VL AND COMPILER_SUPPORTS_MAVX512VBMI)
      add_library(ricepp_bmi2_avx512 OBJECT ricepp_cpuspecific.cpp)
      target_compile_options(ricepp_bmi2_avx512 PRIVATE -mbmi2 -mavx512vl -mavx512vbmi)
      target_compile_definitions(ricepp_bmi2_avx512 PRIVATE RICEPP_CPU_VARIANT=has_bmi2_avx512)
      list(APPEND RICEPP_LIBS_CPUSPECIFIC ricepp_bmi2_avx512)
      list(APPEND RICEPP_CPU_SUPPORT RICEPP_CPU_BMI2_AVX512)
    endif()
  endif()
endif()

foreach(target ${RICEPP_LIBS_CPUSPECIFIC})
  message(STATUS "[ricepp] adding CPU target: ${target}")
  target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  # target_link_libraries(${target} PUBLIC range-v3::range-v3)
  target_include_directories(${target} SYSTEM PUBLIC
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:range-v3::range-v3,INTERFACE_INCLUDE_DIRECTORIES>>
  )
  target_compile_features(${target} PUBLIC cxx_std_20)
  list(APPEND RICEPP_OBJECTS_CPUSPECIFIC $<TARGET_OBJECTS:${target}>)
  list(APPEND RICEPP_OBJECT_TARGETS ${target})
endforeach()

add_library(ricepp-core OBJECT ricepp.cpp cpu_variant.cpp)
# target_link_libraries(ricepp-core PUBLIC range-v3::range-v3)
target_include_directories(ricepp-core SYSTEM PUBLIC
  $<BUILD_INTERFACE:$<TARGET_PROPERTY:range-v3::range-v3,INTERFACE_INCLUDE_DIRECTORIES>>
)
target_include_directories(ricepp-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(ricepp-core PUBLIC cxx_std_20)
target_compile_definitions(ricepp-core PRIVATE ${RICEPP_CPU_SUPPORT})
list(APPEND RICEPP_OBJECT_TARGETS ricepp-core)

set(RICEPP_OBJECT_TARGETS "${RICEPP_OBJECT_TARGETS}" PARENT_SCOPE)

add_library(ricepp)
target_link_libraries(ricepp PUBLIC ${RICEPP_OBJECT_TARGETS})

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  message(STATUS "[ricepp] building standalone")

  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/ricepp CACHE STRING
      "CMake package config files install location")

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ricepp-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ricepp-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
    PATH_VARS
      CMAKE_INSTALL_INCLUDEDIR
      CMAKE_INSTALL_DIR
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ricepp-config-version.cmake
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
  )

  install(
    TARGETS ricepp
    EXPORT ricepp-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/ricepp-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ricepp
  )

  install(
    EXPORT ricepp-targets
    FILE ricepp-targets.cmake
    NAMESPACE ricepp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ricepp
  )
else()
  message(STATUS "[ricepp] building as subproject")
endif()

# # TODO: remove/rework
# add_executable(ricepp_demo ricepp_demo.cpp)
# target_link_libraries(ricepp_demo PRIVATE ricepp fmt)

if(WITH_BENCHMARKS)
  find_package(benchmark 1.8)
  if(benchmark_FOUND)
    if(STATIC_BUILD_DO_NOT_USE)
      add_link_options(-static -static-libgcc)
    endif()

    add_executable(ricepp_benchmark ricepp_benchmark.cpp)
    target_link_libraries(ricepp_benchmark PRIVATE ricepp benchmark::benchmark)

    add_executable(ricepp_benchmark_fits ricepp_benchmark_fits.cpp)
    target_link_libraries(ricepp_benchmark_fits PRIVATE ricepp benchmark::benchmark)

    if (JEMALLOC_FOUND)
      target_link_libraries(ricepp_benchmark PRIVATE PkgConfig::JEMALLOC)
      target_link_libraries(ricepp_benchmark_fits PRIVATE PkgConfig::JEMALLOC)
    endif()
    if(mimalloc_FOUND)
      target_link_libraries(ricepp_benchmark PRIVATE mimalloc-static)
      target_link_libraries(ricepp_benchmark_fits PRIVATE mimalloc-static)
    endif()
  endif()
endif()

if(WITH_TESTS)
  if(NOT TARGET gtest)
    message(STATUS "[ricepp] fetching googletest")
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
      EXCLUDE_FROM_ALL
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

  if (JEMALLOC_FOUND)
    target_link_libraries(ricepp_test PRIVATE PkgConfig::JEMALLOC)
  endif()
  if (mimalloc_FOUND)
    target_link_libraries(ricepp_test PRIVATE mimalloc-static)
  endif()

  if(ENABLE_COVERAGE)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
      foreach(target ricepp ${RICEPP_OBJECT_TARGETS} ricepp_test)
        target_compile_options(
          ${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping
                            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fprofile-instr-generate
                                              -fcoverage-mapping)
      endforeach()
    endif()
  endif()

  if(NOT CMAKE_CROSSCOMPILING)
    gtest_discover_tests(ricepp_test
      DISCOVERY_TIMEOUT 120
      PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profile/%32m.profraw"
    )
  endif()
endif()
