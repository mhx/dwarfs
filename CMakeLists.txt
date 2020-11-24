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

add_subdirectory(folly EXCLUDE_FROM_ALL)

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
  src/dwarfs/filesystem_writer.cpp
  src/dwarfs/fstypes.cpp
  src/dwarfs/inode_manager.cpp
  src/dwarfs/inode_reader.cpp
  src/dwarfs/logger.cpp
  src/dwarfs/lua_script.cpp
  src/dwarfs/metadata.cpp
  src/dwarfs/metadata_writer.cpp
  src/dwarfs/mmap.cpp
  src/dwarfs/options.cpp
  src/dwarfs/os_access_posix.cpp
  src/dwarfs/progress.cpp
  src/dwarfs/scanner.cpp
  src/dwarfs/similarity.cpp
  src/dwarfs/util.cpp)

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

foreach(tgt dwarfs ${BINARY_TARGETS})
  target_include_directories(
    ${tgt} SYSTEM
    PRIVATE ${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/folly
            ${CMAKE_CURRENT_SOURCE_DIR}/folly)

  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_definitions(
    ${tgt}
    PRIVATE DWARFS_VERSION=\"${DWARFS_VERSION}\"
            $<$<BOOL:${LIBLZ4_FOUND}>:DWARFS_HAVE_LIBLZ4>
            $<$<BOOL:${LIBLZMA_FOUND}>:DWARFS_HAVE_LIBLZMA>
            $<$<BOOL:${LIBZSTD_FOUND}>:DWARFS_HAVE_LIBZSTD>
            $<$<BOOL:${WITH_LUA}>:DWARFS_HAVE_LUA>)

  target_compile_options(${tgt} PRIVATE -Wall -Wextra -pedantic)

  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS OFF)
endforeach()

target_compile_definitions(dwarfs-bin PRIVATE FUSE_USE_VERSION=35
                                              _FILE_OFFSET_BITS=64)

foreach(tgt ${BINARY_TARGETS})
  target_link_libraries(
    ${tgt}
    dwarfs
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
