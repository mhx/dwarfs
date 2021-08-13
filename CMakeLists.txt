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
project(dwarfs)
include(ExternalProject)

cmake_minimum_required(VERSION 3.13.4)

include(CheckCXXSourceCompiles)

option(WITH_TESTS "build with tests" OFF)
option(WITH_BENCHMARKS "build with benchmarks" OFF)
option(WITH_PYTHON "build with Python scripting support" OFF)
option(WITH_LEGACY_FUSE "build fuse2 driver even if we have fuse3" OFF)
option(ENABLE_ASAN "enable address sanitizer" OFF)
option(ENABLE_TSAN "enable thread sanitizer" OFF)
option(ENABLE_UBSAN "enable undefined behaviour sanitizer" OFF)
option(ENABLE_COVERAGE "enable code coverage" OFF)
option(USE_JEMALLOC "build with jemalloc" ON)
option(PREFER_SYSTEM_ZSTD "use system zstd if available" OFF)
option(PREFER_SYSTEM_XXHASH "use system xxHash if available" OFF)
option(PREFER_SYSTEM_GTEST "use system gtest if available" OFF)
option(STATIC_BUILD_DO_NOT_USE "try static build (experimental)" OFF)

set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}'")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Build Type" FORCE)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-fdiagnostics-color=always)
  # For gcc, -O3 is *much* worse than -O2
  set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -g")
  set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -g")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_compile_options(-fcolor-diagnostics)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0.0")
    # for some reason, clang-10 binaries crash upon throwing exceptions
    add_compile_options(-fno-omit-frame-pointer)
  endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/version.cmake)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules")

if(STATIC_BUILD_DO_NOT_USE)
  if(WITH_PYTHON)
    message(FATAL_ERROR "python is not supported in static builds")
  endif()
  set(CMAKE_FIND_LIBRARY_SUFFIXES
      ".a"
      CACHE STRING "please look for static libs")
  set(Boost_USE_STATIC_LIBS
      ON
      CACHE BOOL "only static boost libs")
  set(BOOST_LINK_STATIC
      "ON"
      CACHE STRING "yes, really")
  set(USE_STATIC_DEPS_ON_UNIX
      ON
      CACHE BOOL "yes")
  set(GFLAGS_SHARED
      OFF
      CACHE BOOL "static")
endif()

list(APPEND DWARFS_BOOST_MODULES date_time filesystem program_options system)

if(WITH_PYTHON)
  # TODO: would be nicer to be able to support a range of python versions
  find_package(Python3 ${WITH_PYTHON_VERSION} EXACT REQUIRED
               COMPONENTS Development)
  list(APPEND DWARFS_BOOST_MODULES
       "python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
  message(
    STATUS
      "Enabling support for Python ${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
  )
endif()

find_package(Boost 1.65 REQUIRED COMPONENTS ${DWARFS_BOOST_MODULES})

if(WITH_PYTHON)
  set(BOOST_PYTHON_LIBS ${Boost_LIBRARIES})
  list(FILTER Boost_LIBRARIES EXCLUDE REGEX python)
  list(FILTER BOOST_PYTHON_LIBS INCLUDE REGEX python)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.9.7)
pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3>=3.4.1)
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=1.8.3)
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=5.2.2)
pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive>=3.1.2)
pkg_check_modules(ZSTD IMPORTED_TARGET libzstd>=1.4.5)
pkg_check_modules(XXHASH IMPORTED_TARGET libxxhash)

if(XXHASH_FOUND)
  list(APPEND CMAKE_REQUIRED_LIBRARIES PkgConfig::XXHASH)
  check_cxx_source_compiles(
    "#include <xxhash.h>
    #if XXH_VERSION_NUMBER < 800
    #error XXH_VERSION_NUMBER < 800
    #endif
    int main() {
      return 0;
    }"
    XXHASH_VERSION_OK)
endif()

if(NOT FUSE_FOUND AND NOT FUSE3_FOUND)
  message(WARNING "No FUSE or FUSE3 library found")
endif()

if(WITH_TESTS)
  find_program(DIFF_BIN diff DOC "compare files line by line" REQUIRED)
  find_program(TAR_BIN tar DOC "an archiving utility" REQUIRED)
endif()

find_program(RONN_EXE ronn DOC "ronn man page generator" REQUIRED)

if(USE_JEMALLOC)
  find_package(Jemalloc REQUIRED)
endif()

set(ZSTD_INCLUDE_DIR
    ""
    CACHE PATH "don't build folly with zstd" FORCE)

set(compiler_only
    ON
    CACHE BOOL "only build thrift compiler")

add_subdirectory(folly EXCLUDE_FROM_ALL)
add_subdirectory(fbthrift EXCLUDE_FROM_ALL)
if(NOT (ZSTD_FOUND AND PREFER_SYSTEM_ZSTD))
  add_subdirectory(zstd/build/cmake EXCLUDE_FROM_ALL)
endif()

set(ZSTD_LIBRARY_RELEASE
    "ZSTD_LIBRARY_RELEASE-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_DEBUG
    "ZSTD_LIBRARY_DEBUG-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)

if(WITH_TESTS)
  if(NOT PREFER_SYSTEM_GTEST)
    # Download and unpack googletest at configure time
    configure_file(CMakeLists.txt.gtest googletest-download/CMakeLists.txt)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download)
    if(result)
      message(FATAL_ERROR "CMake step for googletest failed: ${result}")
    endif()
    execute_process(
      COMMAND ${CMAKE_COMMAND} --build .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download)
    if(result)
      message(FATAL_ERROR "Build step for googletest failed: ${result}")
    endif()

    # Prevent overriding the parent project's compiler/linker settings on
    # Windows
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)

    # Add googletest directly to our build. This defines the gtest and
    # gtest_main targets.
    add_subdirectory(
      ${CMAKE_CURRENT_BINARY_DIR}/googletest-src
      ${CMAKE_CURRENT_BINARY_DIR}/googletest-build EXCLUDE_FROM_ALL)
  endif()

  enable_testing()
  include(GoogleTest)
endif()

if(NOT
   (XXHASH_FOUND
    AND XXHASH_VERSION_OK
    AND PREFER_SYSTEM_XXHASH))
  add_library(xxhash xxHash/xxhash.c)

  target_compile_options(
    xxhash
    PRIVATE -Wall
            -Wextra
            -Wconversion
            -Wcast-qual
            -Wcast-align
            -Wshadow
            -Wstrict-aliasing=1
            -Wswitch-enum
            -Wdeclaration-after-statement
            -Wstrict-prototypes
            -Wundef
            -Wpointer-arith
            -Wformat-security
            -Wvla
            -Wformat=2
            -Winit-self
            -Wfloat-equal
            -Wwrite-strings
            -Wredundant-decls
            -Wstrict-overflow=2)
endif()

list(
  APPEND
  LIBDWARFS_SRC
  src/dwarfs/block_cache.cpp
  src/dwarfs/block_compressor.cpp
  src/dwarfs/block_manager.cpp
  src/dwarfs/checksum.cpp
  src/dwarfs/console_writer.cpp
  src/dwarfs/entry.cpp
  src/dwarfs/error.cpp
  src/dwarfs/filesystem_extractor.cpp
  src/dwarfs/filesystem_v2.cpp
  src/dwarfs/filesystem_writer.cpp
  src/dwarfs/fstypes.cpp
  src/dwarfs/fs_section.cpp
  src/dwarfs/global_entry_data.cpp
  src/dwarfs/inode_manager.cpp
  src/dwarfs/inode_reader_v2.cpp
  src/dwarfs/logger.cpp
  src/dwarfs/metadata_types.cpp
  src/dwarfs/metadata_v2.cpp
  src/dwarfs/mmap.cpp
  src/dwarfs/nilsimsa.cpp
  src/dwarfs/options.cpp
  src/dwarfs/os_access_posix.cpp
  src/dwarfs/progress.cpp
  src/dwarfs/scanner.cpp
  src/dwarfs/similarity.cpp
  src/dwarfs/string_table.cpp
  src/dwarfs/terminal.cpp
  src/dwarfs/util.cpp
  src/dwarfs/version.cpp
  src/dwarfs/worker_group.cpp)

if(WITH_PYTHON)
  list(APPEND LIBDWARFS_SRC src/dwarfs/python_script.cpp)
endif()

add_library(dwarfs ${LIBDWARFS_SRC})

if(STATIC_BUILD_DO_NOT_USE)
  add_link_options(-static -static-libgcc)
endif(STATIC_BUILD_DO_NOT_USE)

add_executable(mkdwarfs src/mkdwarfs.cpp)
add_executable(dwarfsck src/dwarfsck.cpp)
add_executable(dwarfsbench src/dwarfsbench.cpp)
add_executable(dwarfsextract src/dwarfsextract.cpp)

list(APPEND BINARY_TARGETS mkdwarfs dwarfsck dwarfsbench dwarfsextract)

if(FUSE3_FOUND)
  add_executable(dwarfs-bin src/dwarfs.cpp)
  target_compile_definitions(dwarfs-bin PRIVATE _FILE_OFFSET_BITS=64
                                                FUSE_USE_VERSION=35)
  set_target_properties(dwarfs-bin PROPERTIES OUTPUT_NAME dwarfs)
  target_link_libraries(dwarfs-bin PkgConfig::FUSE3)
  add_custom_target(mount.dwarfs ALL COMMAND ${CMAKE_COMMAND} -E create_symlink
                                             dwarfs mount.dwarfs)
  install(TARGETS dwarfs-bin RUNTIME DESTINATION sbin)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs DESTINATION sbin)
  list(APPEND BINARY_TARGETS dwarfs-bin)
endif()

if(FUSE_FOUND AND (WITH_LEGACY_FUSE OR NOT FUSE3_FOUND))
  add_executable(dwarfs2-bin src/dwarfs.cpp)
  target_compile_definitions(dwarfs2-bin PRIVATE _FILE_OFFSET_BITS=64
                                                 FUSE_USE_VERSION=29)
  set_target_properties(dwarfs2-bin PROPERTIES OUTPUT_NAME dwarfs2)
  target_link_libraries(dwarfs2-bin PkgConfig::FUSE)
  add_custom_target(
    mount.dwarfs2 ALL COMMAND ${CMAKE_COMMAND} -E create_symlink dwarfs2
                              mount.dwarfs2)
  install(TARGETS dwarfs2-bin RUNTIME DESTINATION sbin)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs2 DESTINATION sbin)
  list(APPEND BINARY_TARGETS dwarfs2-bin)
endif()

if(WITH_TESTS OR WITH_BENCHMARKS)
  add_library(test_helpers test/test_helpers.cpp test/test_strings.cpp
                           test/loremipsum.cpp)
  target_link_libraries(test_helpers dwarfs folly)
  set_property(TARGET test_helpers PROPERTY CXX_STANDARD 17)
endif()

if(WITH_TESTS)
  add_executable(dwarfs_test test/dwarfs.cpp)
  add_executable(dwarfs_compat_test test/dwarfs_compat.cpp)
  add_executable(dwarfs_badfs_test test/dwarfs_badfs.cpp)
  add_executable(dwarfs_tools_test test/dwarfs_tools.cpp)

  target_link_libraries(dwarfs_test test_helpers gtest gtest_main)
  target_link_libraries(dwarfs_compat_test gtest gtest_main)
  target_link_libraries(dwarfs_badfs_test gtest gtest_main)
  target_link_libraries(dwarfs_tools_test test_helpers gtest gtest_main)

  list(APPEND BINARY_TARGETS dwarfs_test dwarfs_compat_test dwarfs_badfs_test
       dwarfs_tools_test)

  gtest_discover_tests(dwarfs_test)
  gtest_discover_tests(dwarfs_compat_test)
  gtest_discover_tests(dwarfs_badfs_test)
  gtest_discover_tests(dwarfs_tools_test)

  target_compile_definitions(dwarfs_compat_test
                             PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\")

  target_compile_definitions(dwarfs_badfs_test
                             PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\")

  target_compile_definitions(
    dwarfs_tools_test
    PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\"
            TOOLS_BIN_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\"
            DIFF_BIN=\"${DIFF_BIN}\" TAR_BIN=\"${TAR_BIN}\")
endif()

if(WITH_BENCHMARKS)
  pkg_check_modules(BENCHMARK IMPORTED_TARGET benchmark)
  add_executable(dwarfs_benchmark test/dwarfs_benchmark.cpp)
  target_link_libraries(dwarfs_benchmark test_helpers PkgConfig::BENCHMARK)
  list(APPEND BINARY_TARGETS dwarfs_benchmark)
endif()

foreach(man dwarfs.1 mkdwarfs.1 dwarfsck.1 dwarfsextract.1 dwarfs-format.5)
  string(REGEX MATCH "^[^.]*" docname "${man}")
  string(REGEX MATCH "[^.]*$" section "${man}")
  set(man_dir "${CMAKE_CURRENT_BINARY_DIR}/man${section}")
  set(man_input "${CMAKE_CURRENT_SOURCE_DIR}/doc/${docname}.md")
  set(man_output "${man_dir}/${man}")

  execute_process(
    COMMAND ${RONN_EXE}
    INPUT_FILE "${man_input}"
    RESULT_VARIABLE ronn_result
    OUTPUT_VARIABLE ronn_output
    ERROR_VARIABLE ronn_error)

  if(${ronn_result} EQUAL 0)
    add_custom_command(
      OUTPUT "${man_output}"
      COMMAND mkdir -p "${man_dir}"
      COMMAND ${RONN_EXE} <"${man_input}" >"${man_output}"
      DEPENDS "${man_input}")
    list(APPEND MAN_PAGES "${man_output}")
    list(APPEND MAN_DIRS "${man_dir}")
  else()
    message(WARNING "${RONN_EXE} failed to process ${man}")
  endif()
endforeach()

list(REMOVE_DUPLICATES MAN_DIRS)
add_custom_target(manpages DEPENDS ${MAN_PAGES})
add_dependencies(mkdwarfs manpages)

list(
  APPEND
  FROZEN_THRIFT_SRC
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_data.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_data.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.tcc
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_constants.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_constants.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_metadata.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_metadata.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_visitation.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_for_each_field.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_visit_union.h)

list(
  APPEND
  METADATA_THRIFT_SRC
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_constants.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_constants.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_for_each_field.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_metadata.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_metadata.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.tcc
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types_custom_protocol.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visit_union.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visitation.h)

add_custom_command(
  OUTPUT ${FROZEN_THRIFT_SRC}
  COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift
  COMMAND
    cp ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/thrift/frozen.thrift
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/
  COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift &&
          ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1 --gen mstch_cpp2 frozen.thrift
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
          ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/thrift/frozen.thrift)

add_custom_command(
  OUTPUT ${METADATA_THRIFT_SRC}
  COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/thrift/metadata.thrift
          thrift/dwarfs/metadata.thrift
  COMMAND
    cd ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs &&
    ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1 --gen mstch_cpp2:frozen2
    metadata.thrift
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
          ${CMAKE_CURRENT_SOURCE_DIR}/thrift/metadata.thrift)

list(
  APPEND
  INCLUDE_DIRS
  ${CMAKE_CURRENT_BINARY_DIR}/folly
  ${CMAKE_CURRENT_BINARY_DIR}/thrift
  ${CMAKE_CURRENT_SOURCE_DIR}/folly
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
  ${CMAKE_CURRENT_BINARY_DIR})

if(NOT (ZSTD_FOUND AND PREFER_SYSTEM_ZSTD))
  list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib)
endif()

if(NOT
   (XXHASH_FOUND
    AND XXHASH_VERSION_OK
    AND PREFER_SYSTEM_XXHASH))
  list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/xxHash)
endif()

if(USE_JEMALLOC)
  list(APPEND INCLUDE_DIRS ${Jemalloc_INCLUDE_DIRS})
endif()

set(DWARFS_USE_EXCEPTION_TRACER OFF)
if(NOT STATIC_BUILD_DO_NOT_USE)
  if(TARGET folly_exception_tracer)
    set(DWARFS_USE_EXCEPTION_TRACER ON)
  endif()
endif()

add_library(
  thrift_light
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/FieldRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/CompactProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/BinaryProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/DebugProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/JSONProtocolCommon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/JSONProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp/protocol/TProtocolException.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp/util/VarintUtils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/gen/module_types_cpp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/Frozen.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/FrozenUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/schema/MemorySchema.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_data.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.cpp)

set_property(TARGET thrift_light PROPERTY CXX_STANDARD 17)

target_include_directories(thrift_light PRIVATE ${INCLUDE_DIRS})

add_library(
  metadata_thrift
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.cpp)

set_property(TARGET metadata_thrift PROPERTY CXX_STANDARD 17)

target_include_directories(metadata_thrift PRIVATE ${INCLUDE_DIRS})

add_dependencies(metadata_thrift thrift_light)

foreach(tgt dwarfs ${BINARY_TARGETS})
  target_include_directories(
    ${tgt} SYSTEM
    PRIVATE ${Boost_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS} ${INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/parallel-hashmap)

  target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_definitions(
    ${tgt}
    PRIVATE DWARFS_HAVE_LIBZSTD
            DWARFS_STATIC_BUILD=${STATIC_BUILD_DO_NOT_USE}
            $<$<BOOL:${USE_JEMALLOC}>:DWARFS_USE_JEMALLOC>
            $<$<BOOL:${LIBLZ4_FOUND}>:DWARFS_HAVE_LIBLZ4>
            $<$<BOOL:${LIBLZMA_FOUND}>:DWARFS_HAVE_LIBLZMA>
            $<$<BOOL:${WITH_PYTHON}>:DWARFS_HAVE_PYTHON>)

  if(DWARFS_USE_EXCEPTION_TRACER)
    target_compile_definitions(${tgt} PRIVATE DWARFS_USE_EXCEPTION_TRACER)
  endif()

  target_compile_options(${tgt} PRIVATE -Wall -Wextra -pedantic)

  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 17)
  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS OFF)

  add_dependencies(${tgt} metadata_thrift)

  if(ENABLE_ASAN)
    target_compile_options(${tgt} PRIVATE -fsanitize=address
                                          -fno-omit-frame-pointer)
    target_link_options(${tgt} PRIVATE -fsanitize=address)
  endif()

  if(ENABLE_TSAN)
    target_compile_options(${tgt} PRIVATE -fsanitize=thread
                                          -fno-omit-frame-pointer)
    target_link_options(${tgt} PRIVATE -fsanitize=thread)
  endif()

  if(ENABLE_UBSAN)
    target_compile_options(${tgt} PRIVATE -fsanitize=undefined
                                          -fno-omit-frame-pointer)
    target_link_options(${tgt} PRIVATE -fsanitize=undefined)
  endif()

  if(ENABLE_COVERAGE)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
      target_compile_options(${tgt} PRIVATE --coverage -fno-omit-frame-pointer)
      target_link_options(${tgt} PRIVATE --coverage)
    elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
      target_compile_options(
        ${tgt} PRIVATE -fprofile-instr-generate -fcoverage-mapping
                       -fno-omit-frame-pointer)
      target_link_options(${tgt} PRIVATE -fprofile-instr-generate
                          -fcoverage-mapping)
    endif()
  endif()
endforeach()

# not sure why exactly, copied from fsst/CMakeLists.txt
if(CMAKE_BUILD_TYPE STREQUAL Release)
  set_source_files_properties(fsst/fsst_avx512.cpp PROPERTIES COMPILE_FLAGS -O1)
endif()

add_library(
  fsst
  fsst/libfsst.cpp fsst/fsst_avx512.cpp fsst/fsst_avx512_unroll1.inc
  fsst/fsst_avx512_unroll2.inc fsst/fsst_avx512_unroll3.inc
  fsst/fsst_avx512_unroll4.inc)

target_include_directories(dwarfs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fsst)

target_link_libraries(
  dwarfs
  metadata_thrift
  thrift_light
  folly
  fsst
  ${Boost_LIBRARIES}
  PkgConfig::LIBLZ4
  PkgConfig::LIBLZMA)

if(NOT STATIC_BUILD_DO_NOT_USE)
  target_link_libraries(dwarfs PkgConfig::LIBARCHIVE)
endif(NOT STATIC_BUILD_DO_NOT_USE)

if(ZSTD_FOUND AND PREFER_SYSTEM_ZSTD)
  target_link_libraries(dwarfs PkgConfig::ZSTD)
else()
  target_link_libraries(dwarfs libzstd_static)
endif()

if(XXHASH_FOUND
   AND XXHASH_VERSION_OK
   AND PREFER_SYSTEM_XXHASH)
  target_link_libraries(dwarfs PkgConfig::XXHASH)
else()
  target_link_libraries(dwarfs xxhash)
endif()

if(WITH_PYTHON)
  target_link_libraries(dwarfs ${BOOST_PYTHON_LIBS} ${Python3_LIBRARIES})
endif()

foreach(tgt ${BINARY_TARGETS})
  target_link_libraries(${tgt} dwarfs)
  if(USE_JEMALLOC)
    target_link_libraries(${tgt} ${Jemalloc_LIBRARIES})
  endif(USE_JEMALLOC)
  if(DWARFS_USE_EXCEPTION_TRACER)
    target_link_libraries(
      ${tgt} -Wl,--whole-archive folly_exception_tracer_base
      folly_exception_tracer -Wl,--no-whole-archive)
  endif()
endforeach()

if(STATIC_BUILD_DO_NOT_USE)
  # ...................................................................
  # libarchive is the module that creates the real pain here Its share version
  # looks around to find  other shared libraries which can do the real work. For
  # example, if liblzma.so is found then libarchive supports lzma encoding
  # Static libary is compiled against predefined set of static archive libraries
  # at the time when binary distribution created. This is not necessarily the
  # set of libraries which is present at the system where this script is
  # executed.
  #
  # cmake-format: off
  #
  # There are two options to fix it:
  #
  #   1) Build local version of libarchive.a
  #
  #   2) Use LIBARCHIVE_STATIC_LIBRARIES and LIBARCHIVE_STATIC_LDFLAGS that are
  #      set by pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive>=3.1.2)
  #
  # cmake-format: on
  #
  # Method #1 is implemented here. This implementation is not pedantic - it uses
  # headers from system libarchive package but libararies from local build.
  # Local build is based on default settings for v3.5.1 with xml and iconv
  # support turned off in order to reduce that set of libraries for futher
  # linkage.
  # ...................................................................

  set(_LIBARCHIVE_PRJ "libarchive")
  set(__LIBARCHIVE "${PROJECT_BINARY_DIR}/libarchive/lib/libarchive.a")

  if(ZSTD_FOUND AND PREFER_SYSTEM_ZSTD)
    set(_LIBARCHIVE_CMAKE_ARGS "")
  else()
    set(_LIBARCHIVE_CMAKE_ARGS
        -DZSTD_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/zstd/lib
        -DCMAKE_LIBRARY_PATH=${PROJECT_BINARY_DIR}/zstd/build/cmake/lib/)
  endif()

  ExternalProject_Add(
    ${_LIBARCHIVE_PRJ}
    PREFIX "${PROJECT_BINARY_DIR}/libarchive"
    GIT_REPOSITORY "https://github.com/libarchive/libarchive.git"
    GIT_TAG "v3.5.1"
    CMAKE_ARGS ${_LIBARCHIVE_CMAKE_ARGS}
               -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/libarchive
               -DCMAKE_BUILD_TYPE=Release
               -DENABLE_ICONV:BOOL=OFF
               -DENABLE_LIBXML2:BOOL=OFF
    BUILD_BYPRODUCTS ${__LIBARCHIVE}
    TMP_DIR "${PROJECT_BINARY_DIR}/libarchive/tmp"
    STAMP_DIR "${PROJECT_BINARY_DIR}/libarchive/stamp"
    SOURCE_DIR "${PROJECT_BINARY_DIR}/libarchive/src"
    BINARY_DIR "${PROJECT_BINARY_DIR}/libarchive/build"
    INSTALL_DIR "")

  if(NOT (ZSTD_FOUND AND PREFER_SYSTEM_ZSTD))
    add_dependencies(${_LIBARCHIVE_PRJ} libzstd_static)
  endif()

  add_library(static_libarchive STATIC IMPORTED)
  set_target_properties(static_libarchive PROPERTIES IMPORTED_LOCATION
                                                     ${__LIBARCHIVE})
  add_dependencies(static_libarchive ${_LIBARCHIVE_PRJ})

  # ...................................................................
  # Each library name given to the NAMES option is first considered as a library
  # file name and then considered with platform-specific prefixes (e.g. lib) and
  # suffixes (e.g. .so).
  # ...................................................................

  function(IMPORT_STATIC_LIB TARGET NAME)
    find_library(_TMP_LIB_LOC_${TARGET} ${NAME} NO_CACHE REQUIRED)
    add_library(${TARGET} STATIC IMPORTED)
    set_target_properties(${TARGET} PROPERTIES IMPORTED_LOCATION
                                               ${_TMP_LIB_LOC_${TARGET}})
  endfunction()

  import_static_lib(static_libglog "libglog.a")
  import_static_lib(static_libfmt "libfmt.a")
  import_static_lib(static_libdoubleconv "libdouble-conversion.a")
  import_static_lib(static_libgflags "libgflags.a")
  import_static_lib(static_libevent "libevent.a")
  import_static_lib(static_libacl "libacl.a")
  import_static_lib(static_libcrypto "libcrypto.a")
  import_static_lib(static_libz "libz.a")
  import_static_lib(static_libpthread "libpthread.a")
  import_static_lib(static_libdl "libdl.a")
  import_static_lib(static_libc "libc.a")
  import_static_lib(static_libm "libm.a")
  import_static_lib(static_librt "librt.a")
  import_static_lib(static_libssl "libssl.a")
  import_static_lib(static_libunwind "libunwind.a")
  set_target_properties(static_libunwind PROPERTIES INTERFACE_LINK_LIBRARIES
                                                    PkgConfig::LIBLZMA)
  set_target_properties(static_libglog PROPERTIES INTERFACE_LINK_LIBRARIES
                                                  static_libgflags)
  set_target_properties(static_librt PROPERTIES INTERFACE_LINK_LIBRARIES
                                                static_libgflags)

  foreach(tgt ${BINARY_TARGETS})
    # ...................................................................
    # -static-libgcc above and gcc_eh below is all together an ugly trick to
    # enforce static linking
    # ...................................................................
    target_link_libraries(
      ${tgt}
      static_libfmt
      static_libdoubleconv
      static_libglog
      static_libgflags
      static_libarchive
      static_libevent
      static_libacl
      static_libssl
      static_libcrypto
      static_libpthread
      static_libdl
      static_libz
      static_libc
      static_libm
      static_librt
      gcc_eh
      static_libunwind)
  endforeach()
endif(STATIC_BUILD_DO_NOT_USE)

add_custom_target(
  realclean
  COMMAND
    rm -rf CMake* CPack* CTest* Makefile Testing bin lib man folly fbthrift
    thrift zstd dwarfs* mkdwarfs mount.dwarfs mount.dwarfs2 lib*.a *.cmake
    googletest-* _CPack_Packages install_manifest.txt share build.ninja
    compile_commands.json)

install(
  TARGETS ${BINARY_TARGETS}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
foreach(man_dir ${MAN_DIRS})
  install(DIRECTORY "${man_dir}" DESTINATION share/man)
endforeach()

# TODO: There's currently no point installing the library + headers, as these
# have dependencies on the bundled folly/thrift/... which we don't install.
if(FALSE)
  if(NOT STATIC_BUILD_DO_NOT_USE)
    install(
      TARGETS dwarfs
      RUNTIME DESTINATION bin
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib)
    install(DIRECTORY include/dwarfs DESTINATION include)
  endif()
endif()

if(NOT $PRJ_VERSION_FULL} STREQUAL "")
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_SOURCE_GENERATOR "${CPACK_GENERATOR}")
  set(CPACK_PACKAGE_VERSION_MAJOR "${PRJ_VERSION_MAJOR}")
  set(CPACK_PACKAGE_VERSION_MINOR "${PRJ_VERSION_MINOR}")
  set(CPACK_PACKAGE_VERSION_PATCH "${PRJ_VERSION_PATCH}")
  set(CPACK_SOURCE_PACKAGE_FILE_NAME
      "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}")
  set(CPACK_PACKAGE_FILE_NAME
      "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "dwarfs - A high compression read-only file system")
  set(CPACK_PACKAGE_VENDOR "Marcus Holland-Moritz <github@mhxnet.de>")
  set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_SOURCE_IGNORE_FILES "\\.git/" "${CMAKE_SOURCE_DIR}/build.*"
                                "${CMAKE_SOURCE_DIR}/@" "/\\." ".*~$")
  set(CPACK_VERBATIM_VARIABLES YES)

  include(CPack)
endif()
