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
option(WITH_LUA "build with Lua scripting support" OFF)

set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}'")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Build Type" FORCE)
endif()

set(PROJECT_VERSION_MAJOR "0")
set(PROJECT_VERSION_MINOR "1")
set(PROJECT_VERSION_PATCH "1")
set(DWARFS_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

find_package(PkgConfig REQUIRED)

find_package(Boost 1.67 REQUIRED COMPONENTS date_time filesystem
                                            program_options system)

pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3>=3.4.1)
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=1.8.3)
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=5.2.4)
pkg_check_modules(LIBZSTD IMPORTED_TARGET libzstd>=1.3.8)

set(compiler_only
    ON
    CACHE BOOL "only build thrift compiler")

add_subdirectory(folly EXCLUDE_FROM_ALL)
add_subdirectory(fbthrift EXCLUDE_FROM_ALL)

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

list(
  APPEND
  LIBDWARFS_SRC
  src/dwarfs/block_cache.cpp
  src/dwarfs/block_compressor.cpp
  src/dwarfs/block_manager.cpp
  src/dwarfs/console_writer.cpp
  src/dwarfs/entry.cpp
  src/dwarfs/filesystem.cpp
  src/dwarfs/filesystem_v2.cpp
  src/dwarfs/filesystem_writer.cpp
  src/dwarfs/fstypes.cpp
  src/dwarfs/inode_manager.cpp
  src/dwarfs/inode_reader.cpp
  src/dwarfs/logger.cpp
  src/dwarfs/metadata.cpp
  src/dwarfs/metadata_v2.cpp
  src/dwarfs/metadata_writer.cpp
  src/dwarfs/mmap.cpp
  src/dwarfs/options.cpp
  src/dwarfs/os_access_posix.cpp
  src/dwarfs/progress.cpp
  src/dwarfs/scanner.cpp
  src/dwarfs/similarity.cpp
  src/dwarfs/util.cpp
  src/dwarfs/worker_group.cpp)

if(WITH_LUA)
  list(APPEND LIBDWARFS_SRC src/dwarfs/lua_script.cpp)
endif()

add_library(dwarfs ${LIBDWARFS_SRC})

add_executable(mkdwarfs src/mkdwarfs.cpp)
add_executable(dwarfs-bin src/dwarfs.cpp)
add_executable(dwarfsck src/dwarfsck.cpp)
add_executable(dwarfsbench src/dwarfsbench.cpp)

list(APPEND BINARY_TARGETS mkdwarfs dwarfs-bin dwarfsck dwarfsbench)

if(WITH_TESTS)
  add_executable(dwarfs_test test/dwarfs.cpp test/loremipsum.cpp)

  target_link_libraries(dwarfs_test gtest_main)

  list(APPEND BINARY_TARGETS dwarfs_test)

  gtest_discover_tests(dwarfs_test)
endif()

add_custom_command(
  OUTPUT
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
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visitation.h
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
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/gen-cpp2/frozen_visit_union.h
  COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift
  COMMAND
    cp ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/thrift/frozen.thrift
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/
  COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/thrift/metadata.thrift
          thrift/dwarfs/metadata.thrift
  COMMAND
    cd ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs &&
    ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1 --gen mstch_cpp2:frozen2
    metadata.thrift
  COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift &&
          ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1 --gen mstch_cpp2 frozen.thrift
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

add_library(
  thrift_light
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/CompactProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/BinaryProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/DebugProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/JSONProtocolCommon.cpp
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

foreach(tgt dwarfs ${BINARY_TARGETS})
  target_include_directories(${tgt} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS}
                                                   ${INCLUDE_DIRS})

  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_definitions(
    ${tgt}
    PRIVATE DWARFS_VERSION=\"${DWARFS_VERSION}\"
            $<$<BOOL:${LIBLZ4_FOUND}>:DWARFS_HAVE_LIBLZ4>
            $<$<BOOL:${LIBLZMA_FOUND}>:DWARFS_HAVE_LIBLZMA>
            $<$<BOOL:${LIBZSTD_FOUND}>:DWARFS_HAVE_LIBZSTD>
            $<$<BOOL:${WITH_LUA}>:DWARFS_HAVE_LUA>)

  target_compile_options(${tgt} PRIVATE -Wall -Wextra -pedantic)

  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 17)
  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS OFF)

  add_dependencies(${tgt} metadata_thrift)
endforeach()

target_compile_definitions(dwarfs-bin PRIVATE FUSE_USE_VERSION=35
                                              _FILE_OFFSET_BITS=64)

foreach(tgt ${BINARY_TARGETS})
  target_link_libraries(
    ${tgt}
    dwarfs
    metadata_thrift
    thrift_light
    folly
    ${Boost_LIBRARIES}
    PkgConfig::LIBLZ4
    PkgConfig::LIBLZMA
    PkgConfig::LIBZSTD)

  if(WITH_LUA)
    target_link_libraries(${tgt} luabind lua)
  endif()
endforeach()

target_link_libraries(dwarfs-bin PkgConfig::FUSE3)

set_target_properties(dwarfs-bin PROPERTIES OUTPUT_NAME dwarfs)

install(
  TARGETS mkdwarfs dwarfs-bin dwarfsck dwarfs dwarfsbench
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
install(FILES man/dwarfs.1 man/mkdwarfs.1 DESTINATION share/man/man1)
install(DIRECTORY include/dwarfs DESTINATION include)

set(CPACK_SOURCE_GENERATOR "TBZ2")
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
    "${CMAKE_SOURCE_DIR}/build/"
    "${CMAKE_SOURCE_DIR}/tmp/"
    "${CMAKE_SOURCE_DIR}/@"
    "${CMAKE_SOURCE_DIR}/.*\\.lua"
    "\\.dwarfs$"
    "/\\."
    ".*~$")
set(CPACK_VERBATIM_VARIABLES YES)

include(CPack)
