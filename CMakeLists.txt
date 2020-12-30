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

cmake_minimum_required(VERSION 3.13.4)

option(WITH_TESTS "build with tests" OFF)
option(WITH_PYTHON "build with Python scripting support" OFF)
option(ENABLE_ASAN "enable address sanitizer" OFF)
option(ENABLE_TSAN "enable thread sanitizer" OFF)
option(ENABLE_UBSAN "enable undefined behaviour sanitizer" OFF)
option(USE_JEMALLOC "build with jemalloc" ON)
option(STATIC_BUILD_DO_NOT_USE "try static build (experimental)" OFF)

set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}'")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Build Type" FORCE)
endif()

set(PROJECT_VERSION_MAJOR "0")
set(PROJECT_VERSION_MINOR "3")
set(PROJECT_VERSION_PATCH "0")
set(DWARFS_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

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

find_package(Boost 1.67 REQUIRED COMPONENTS ${DWARFS_BOOST_MODULES})

if(WITH_PYTHON)
  set(BOOST_PYTHON_LIBS ${Boost_LIBRARIES})
  list(FILTER Boost_LIBRARIES EXCLUDE REGEX python)
  list(FILTER BOOST_PYTHON_LIBS INCLUDE REGEX python)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.9.9)
pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3>=3.4.1)
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=1.8.3)
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=5.2.4)

if(NOT FUSE_FOUND AND NOT FUSE3_FOUND)
  message(FATAL_ERROR "No FUSE or FUSE3 library found")
endif()

find_path(SPARSEHASH_INCLUDE_DIR sparsehash/dense_hash_map
          DOC "dense_hash_map header" REQUIRED)
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
add_subdirectory(zstd/build/cmake EXCLUDE_FROM_ALL)

set(ZSTD_LIBRARY_RELEASE
    "ZSTD_LIBRARY_RELEASE-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_DEBUG
    "ZSTD_LIBRARY_DEBUG-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)

if(WITH_TESTS)
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

  # Prevent overriding the parent project's compiler/linker settings on Windows
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)

  # Add googletest directly to our build. This defines the gtest and gtest_main
  # targets.
  add_subdirectory(
    ${CMAKE_CURRENT_BINARY_DIR}/googletest-src
    ${CMAKE_CURRENT_BINARY_DIR}/googletest-build EXCLUDE_FROM_ALL)

  enable_testing()
  include(GoogleTest)
endif()

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
  src/dwarfs/terminal.cpp
  src/dwarfs/util.cpp
  src/dwarfs/worker_group.cpp)

if(WITH_PYTHON)
  list(APPEND LIBDWARFS_SRC src/dwarfs/python_script.cpp)
endif()

add_library(dwarfs ${LIBDWARFS_SRC})

add_executable(mkdwarfs src/mkdwarfs.cpp)
add_executable(dwarfsck src/dwarfsck.cpp)
add_executable(dwarfsbench src/dwarfsbench.cpp)

list(APPEND BINARY_TARGETS mkdwarfs dwarfsck dwarfsbench)

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

if(FUSE_FOUND)
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

if(WITH_TESTS)
  add_executable(dwarfs_test test/dwarfs.cpp test/loremipsum.cpp)
  add_executable(dwarfs_compat_test test/dwarfs_compat.cpp)

  target_link_libraries(dwarfs_test gtest_main)
  target_link_libraries(dwarfs_compat_test gtest_main)

  list(APPEND BINARY_TARGETS dwarfs_test dwarfs_compat_test)

  gtest_discover_tests(dwarfs_test)
  gtest_discover_tests(dwarfs_compat_test)

  target_compile_definitions(dwarfs_compat_test
                             PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\")
endif()

foreach(man dwarfs mkdwarfs)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/man/${man}.1
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/man
    COMMAND ${RONN_EXE} <${CMAKE_CURRENT_SOURCE_DIR}/doc/${man}.md
            >${CMAKE_CURRENT_BINARY_DIR}/man/${man}.1
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/doc/${man}.md)
  list(APPEND MAN_PAGES ${CMAKE_CURRENT_BINARY_DIR}/man/${man}.1)
endforeach()

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
  ${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/xxHash
  ${CMAKE_CURRENT_BINARY_DIR}
  ${SPARSEHASH_INCLUDE_DIR})

if(USE_JEMALLOC)
  list(APPEND INCLUDE_DIRS ${Jemalloc_INCLUDE_DIRS})
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
    ${tgt} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS}
                          ${INCLUDE_DIRS})

  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_definitions(
    ${tgt}
    PRIVATE DWARFS_VERSION=\"${DWARFS_VERSION}\"
            DWARFS_HAVE_LIBZSTD
            DWARFS_STATIC_BUILD=${STATIC_BUILD_DO_NOT_USE}
            $<$<BOOL:${USE_JEMALLOC}>:DWARFS_USE_JEMALLOC>
            $<$<BOOL:${LIBLZ4_FOUND}>:DWARFS_HAVE_LIBLZ4>
            $<$<BOOL:${LIBLZMA_FOUND}>:DWARFS_HAVE_LIBLZMA>
            $<$<BOOL:${WITH_PYTHON}>:DWARFS_HAVE_PYTHON>)

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
endforeach()

target_link_libraries(
  dwarfs
  metadata_thrift
  thrift_light
  folly
  ${Boost_LIBRARIES}
  libzstd_static
  xxhash
  PkgConfig::LIBLZ4
  PkgConfig::LIBLZMA)

foreach(tgt ${BINARY_TARGETS})
  target_link_libraries(${tgt} dwarfs)
  if(USE_JEMALLOC)
    target_link_libraries(${tgt} ${Jemalloc_LIBRARIES})
  endif()
  if(TARGET folly_exception_tracer)
    target_link_libraries(
      ${tgt} -Wl,--whole-archive folly_exception_tracer_base
      folly_exception_tracer -Wl,--no-whole-archive)
  endif()
endforeach()

if(WITH_PYTHON)
  target_link_libraries(mkdwarfs ${BOOST_PYTHON_LIBS} ${Python3_LIBRARIES})

  if(WITH_TESTS)
    foreach(tgt dwarfs_test dwarfs_compat_test)
      target_link_libraries(${tgt} ${BOOST_PYTHON_LIBS} ${Python3_LIBRARIES})
    endforeach()
  endif()
endif()

if(STATIC_BUILD_DO_NOT_USE)
  # This is a proper mess, but it does work somehow...
  set(CMAKE_CXX_LINK_EXECUTABLE
      "/bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/cmake/static_link.sh <TARGET> <OBJECTS>"
  )
endif()

add_custom_target(
  realclean
  COMMAND
    rm -rf CMake* CPack* CTest* Makefile Testing bin lib man folly fbthrift
    thrift zstd dwarfs* mkdwarfs mount.dwarfs mount.dwarfs2 lib*.a *.cmake
    googletest-* _CPack_Packages install_manifest.txt share)

install(
  TARGETS mkdwarfs dwarfsck dwarfsbench
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
install(FILES ${MAN_PAGES} DESTINATION share/man/man1)

if(NOT STATIC_BUILD_DO_NOT_USE)
  install(
    TARGETS dwarfs
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
  install(DIRECTORY include/dwarfs DESTINATION include)
endif()

set(CPACK_GENERATOR "TGZ;TBZ2;TXZ")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${DWARFS_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "dwarfs - A high compression read-only file system")
set(CPACK_PACKAGE_VENDOR "Marcus Holland-Moritz <github@mhxnet.de>")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_IGNORE_FILES
    "\\.git/"
    "${CMAKE_SOURCE_DIR}/build.*"
    "${CMAKE_SOURCE_DIR}/tmp/"
    "${CMAKE_SOURCE_DIR}/@"
    "\\.dwarfs$"
    "/\\."
    ".*~$")
set(CPACK_VERBATIM_VARIABLES YES)

include(CPack)
