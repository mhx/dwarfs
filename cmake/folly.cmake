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

set(FOLLY_NO_EXCEPTION_TRACER ON CACHE BOOL "disable exception tracer")
set(ZSTD_INCLUDE_DIR "" CACHE PATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_RELEASE
    "ZSTD_LIBRARY_RELEASE-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_DEBUG
    "ZSTD_LIBRARY_DEBUG-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(FOLLY_USE_JEMALLOC OFF CACHE BOOL "don't build folly with jemalloc" FORCE)
endif()

# TODO: this is due to a bug in folly's Portability.h
add_compile_definitions(FOLLY_CFG_NO_COROUTINES)

add_compile_definitions(GLOG_NO_ABBREVIATED_SEVERITIES NOMINMAX NOGDI)

# TODO: temporary workaround until this is fixed in folly
#       see https://github.com/facebook/folly/issues/2149
add_compile_definitions(GLOG_USE_GLOG_EXPORT)

set(
  CXX_STD "gnu++${DWARFS_CXX_STANDARD}"
  CACHE STRING
  "The C++ standard argument to pass to the compiler."
)

set(
  MSVC_LANGUAGE_VERSION "c++${DWARFS_CXX_STANDARD}"
  CACHE STRING
  "The C++ standard argument to pass to the compiler."
)

set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON)
set(CMAKE_DISABLE_FIND_PACKAGE_Snappy ON)
set(CMAKE_DISABLE_FIND_PACKAGE_LibAIO ON)
set(CMAKE_DISABLE_FIND_PACKAGE_LibUring ON)
set(CMAKE_DISABLE_FIND_PACKAGE_Libsodium ON)
set(CMAKE_DISABLE_FIND_PACKAGE_LibDwarf ON)

if(NOT PREFER_SYSTEM_FAST_FLOAT)
  set(FASTFLOAT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fast_float)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/folly EXCLUDE_FROM_ALL SYSTEM)

if(NOT PREFER_SYSTEM_FAST_FLOAT)
  get_target_property(_tmpdirs folly_deps INTERFACE_INCLUDE_DIRECTORIES)
  list(REMOVE_ITEM _tmpdirs "${CMAKE_CURRENT_SOURCE_DIR}/fast_float")
  set_target_properties(folly_deps PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_tmpdirs}")
endif()

if(NOT DWARFS_FMT_LIB)
  get_target_property(FOLLY_DEPS_INTERFACE_LINK_LIBRARIES folly_deps INTERFACE_LINK_LIBRARIES)
  list(REMOVE_ITEM FOLLY_DEPS_INTERFACE_LINK_LIBRARIES fmt::fmt)
  set_target_properties(folly_deps PROPERTIES INTERFACE_LINK_LIBRARIES "${FOLLY_DEPS_INTERFACE_LINK_LIBRARIES}")
endif()

# remove dependencies that are not needed
get_target_property(_tmp_folly_deps folly_deps INTERFACE_LINK_LIBRARIES)
list(REMOVE_ITEM _tmp_folly_deps ${LIBEVENT_LIB} ${OPENSSL_LIBRARIES})
list(REMOVE_ITEM _tmp_folly_deps Boost::context Boost::atomic Boost::regex Boost::system)
if(NOT WIN32)
  list(REMOVE_ITEM _tmp_folly_deps Boost::thread)
endif()
set_target_properties(folly_deps PROPERTIES INTERFACE_LINK_LIBRARIES "${_tmp_folly_deps}")

add_library(dwarfs_folly_lite OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Conv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Demangle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ExceptionString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/File.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/FileUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ScopeGuard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/String.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Unicode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/container/detail/F14Table.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/FileUtilDetail.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/hash/SpookyHashV2.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBuf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBufQueue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/dynamic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/json_pointer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/CString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/Exception.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/SafeAssert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/ToAscii.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/Malloc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/SanitizeAddress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/SanitizeLeak.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/detail/MallocImpl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/net/NetOps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Stdlib.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysUio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Unistd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/QuantileEstimator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/TDigest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/detail/DoubleRadixSort.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/HardwareConcurrency.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/ThreadName.cpp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # silence warning until this is fixed upstream
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/net/NetOps.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-address"
  )
endif()

if(WIN32)
  target_sources(dwarfs_folly_lite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/net/detail/SocketFileDescriptorMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Fcntl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/PThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Sockets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysMman.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysResource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysStat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Time.cpp
  )
endif()

set_property(TARGET dwarfs_folly_lite PROPERTY CXX_STANDARD ${DWARFS_CXX_STANDARD})
target_include_directories(
  dwarfs_folly_lite SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/folly>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/folly>
)

# On platforms other than x86 / arm, we need to use the fallback
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|aarch64")
  target_compile_definitions(dwarfs_folly_lite PUBLIC FOLLY_F14_FORCE_FALLBACK=1)
endif()

if(NOT PREFER_SYSTEM_FAST_FLOAT)
  target_include_directories(
    dwarfs_folly_lite SYSTEM PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fast_float>
  )
endif()

apply_folly_compile_options_to_target(dwarfs_folly_lite)
target_link_libraries(dwarfs_folly_lite PUBLIC folly_deps)

foreach(tgt dwarfs_folly_lite)
  if(TARGET ${tgt})
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      # See: https://github.com/cpp-best-practices/cppbestpractices/blob/master/02-Use_the_Tools_Available.md
      target_compile_options(${tgt} PRIVATE
        /wd4189
        /wd4242
        /wd4458
        /wd4866
        /wd5039
        /wd5246
      )
    endif()
  endif()
endforeach()
