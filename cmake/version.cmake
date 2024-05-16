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

set(VERSION_SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/dwarfs/version.cpp)
set(VERSION_HDR_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/dwarfs/version.h)
set(PKG_VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_version.cmake)

if("${NIXPKGS_DWARFS_VERSION_OVERRIDE}" STREQUAL "")
  execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --show-toplevel
    OUTPUT_VARIABLE GIT_TOPLEVEL_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

  execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" log -1 --format=%h --abbrev=10
    OUTPUT_VARIABLE PRJ_GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

  execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" log -1 --format=%cs
    OUTPUT_VARIABLE PRJ_GIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

get_filename_component(GIT_TOPLEVEL "${GIT_TOPLEVEL_RAW}" REALPATH)
get_filename_component(REAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)

message(STATUS "REAL_SOURCE_DIR: ${REAL_SOURCE_DIR} (${CMAKE_CURRENT_SOURCE_DIR})")
message(STATUS "GIT_TOPLEVEL: ${GIT_TOPLEVEL} (${GIT_TOPLEVEL_RAW})")
message(STATUS "PRJ_GIT_REV: ${PRJ_GIT_REV}")
message(STATUS "PRJ_GIT_DATE: ${PRJ_GIT_DATE}")

if(((NOT "${REAL_SOURCE_DIR}" STREQUAL "${GIT_TOPLEVEL}")
   OR ("${PRJ_GIT_REV}" STREQUAL ""))
   AND ("${NIXPKGS_DWARFS_VERSION_OVERRIDE}" STREQUAL ""))
  if(NOT EXISTS ${VERSION_SRC_FILE} OR NOT EXISTS ${VERSION_HDR_FILE} OR NOT EXISTS ${PKG_VERSION_FILE})
    message(FATAL_ERROR "missing version files")
  endif()

  include(${PKG_VERSION_FILE})

  message(STATUS "PRJ_VERSION_FULL: ${PRJ_VERSION_FULL}")

  set(DWARFS_GIT_BUILD OFF)
else()
  if(EXISTS ${VERSION_SRC_FILE} OR EXISTS ${VERSION_HDR_FILE} OR EXISTS ${PKG_VERSION_FILE})
    message(FATAL_ERROR "version files must not exist in git repository")
  endif()

  set(DWARFS_GIT_BUILD ON)

  set(TMP_PKG_VERSION_FILE ${CMAKE_CURRENT_BINARY_DIR}/package_version.cmake)
  set(TMP_VERSION_SRC_FILE ${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs/version.cpp)
  set(TMP_VERSION_HDR_FILE ${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs/version.h)

  if ("${NIXPKGS_DWARFS_VERSION_OVERRIDE}" STREQUAL "")
    execute_process(
      COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --match "v*" --exact-match
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE PRJ_GIT_RELEASE_TAG)
    execute_process(
      COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --match "v*" --dirty --abbrev=10
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE PRJ_GIT_DESC)
    execute_process(
      COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE PRJ_GIT_BRANCH)
  else()
    set(PRJ_GIT_REV "NIXPKGS")
    set(PRJ_GIT_RELEASE_TAG "${NIXPKGS_DWARFS_VERSION_OVERRIDE}")
    set(PRJ_GIT_DESC "${NIXPKGS_DWARFS_VERSION_OVERRIDE}")
    set(PRJ_GIT_BRANCH "HEAD")
  endif()

  string(STRIP "${PRJ_GIT_REV}" PRJ_GIT_REV)
  string(STRIP "${PRJ_GIT_DATE}" PRJ_GIT_DATE)
  string(STRIP "${PRJ_GIT_DESC}" PRJ_GIT_DESC)
  string(STRIP "${PRJ_GIT_BRANCH}" PRJ_GIT_BRANCH)
  string(SUBSTRING "${PRJ_GIT_DESC}" 1 -1 PRJ_VERSION_FULL)

  string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" PRJ_VERSION_MAJOR
                       "${PRJ_GIT_DESC}")
  string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" PRJ_VERSION_MINOR
                       "${PRJ_GIT_DESC}")
  string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" PRJ_VERSION_PATCH
                       "${PRJ_GIT_DESC}")

  set(PRJ_VERSION_SHORT "${PRJ_VERSION_MAJOR}.${PRJ_VERSION_MINOR}.${PRJ_VERSION_PATCH}")

  set(PRJ_GIT_ID ${PRJ_GIT_DESC})

  if(NOT PRJ_GIT_RELEASE_TAG)
    set(PRJ_GIT_ID "${PRJ_GIT_ID} on branch ${PRJ_GIT_BRANCH}")
  endif()

  if(PRJ_GIT_DATE)
    set(PRJ_GIT_DATE_VALUE "\"${PRJ_GIT_DATE}\"")
  else()
    set(PRJ_GIT_DATE_VALUE "nullptr")
  endif()

  set(PKG_VERSION
      "# autogenerated code, do not modify

set(PRJ_GIT_REV \"${PRJ_GIT_REV}\")
set(PRJ_GIT_DATE \"${PRJ_GIT_DATE}\")
set(PRJ_GIT_DESC \"${PRJ_GIT_DESC}\")
set(PRJ_GIT_BRANCH \"${PRJ_GIT_BRANCH}\")
set(PRJ_GIT_ID \"${PRJ_GIT_ID}\")
set(PRJ_GIT_RELEASE_TAG \"${PRJ_GIT_RELEASE_TAG}\")
set(PRJ_VERSION_FULL \"${PRJ_VERSION_FULL}\")
set(PRJ_VERSION_SHORT \"${PRJ_VERSION_SHORT}\")
set(PRJ_VERSION_MAJOR \"${PRJ_VERSION_MAJOR}\")
set(PRJ_VERSION_MINOR \"${PRJ_VERSION_MINOR}\")
set(PRJ_VERSION_PATCH \"${PRJ_VERSION_PATCH}\")
")

  set(VERSION_SRC
      "// autogenerated code, do not modify

#include \"dwarfs/version.h\"

namespace dwarfs {

char const* PRJ_GIT_REV = \"${PRJ_GIT_REV}\";
char const* PRJ_GIT_DATE = ${PRJ_GIT_DATE_VALUE};
char const* PRJ_GIT_DESC = \"${PRJ_GIT_DESC}\";
char const* PRJ_GIT_BRANCH = \"${PRJ_GIT_BRANCH}\";
char const* PRJ_GIT_ID = \"${PRJ_GIT_ID}\";

} // namespace dwarfs
")

  set(VERSION_HDR
      "// autogenerated code, do not modify

#pragma once

#define PRJ_VERSION_MAJOR ${PRJ_VERSION_MAJOR}
#define PRJ_VERSION_MINOR ${PRJ_VERSION_MINOR}
#define PRJ_VERSION_PATCH ${PRJ_VERSION_PATCH}

namespace dwarfs {

extern char const* PRJ_GIT_REV;
extern char const* PRJ_GIT_DATE;
extern char const* PRJ_GIT_DESC;
extern char const* PRJ_GIT_BRANCH;
extern char const* PRJ_GIT_ID;

} // namespace dwarfs
")

  if(EXISTS ${TMP_PKG_VERSION_FILE})
    file(READ ${TMP_PKG_VERSION_FILE} PKG_VERSION_OLD)
  else()
    set(PKG_VERSION_OLD "")
  endif()

  if(EXISTS ${TMP_VERSION_SRC_FILE})
    file(READ ${TMP_VERSION_SRC_FILE} VERSION_SRC_OLD)
  else()
    set(VERSION_SRC_OLD "")
  endif()

  if(EXISTS ${TMP_VERSION_HDR_FILE})
    file(READ ${TMP_VERSION_HDR_FILE} VERSION_HDR_OLD)
  else()
    set(VERSION_HDR_OLD "")
  endif()

  if(NOT "${PKG_VERSION}" STREQUAL "${PKG_VERSION_OLD}")
    file(WRITE ${TMP_PKG_VERSION_FILE} "${PKG_VERSION}")
  endif()

  if(NOT "${VERSION_SRC}" STREQUAL "${VERSION_SRC_OLD}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs")
    file(WRITE ${TMP_VERSION_SRC_FILE} "${VERSION_SRC}")
  endif()

  if(NOT "${VERSION_HDR}" STREQUAL "${VERSION_HDR_OLD}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs")
    file(WRITE ${TMP_VERSION_HDR_FILE} "${VERSION_HDR}")
  endif()
endif()
