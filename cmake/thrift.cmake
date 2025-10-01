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

if(APPLE)
  # For whatever reason, thrift is unhappy if we don't do this
  find_package(OpenSSL 1.1.1 MODULE REQUIRED)
endif()

if(DWARFS_GIT_BUILD)
  set(THRIFT_COMPILER_ONLY ON CACHE BOOL "only build thrift compiler")
  # We need to fake a folly module for fbthrift, but we just alias our
  # dwarfs_folly_lite target. Fortunately, we only need this hack in
  # a git build. :-)
  set(folly_DIR "${CMAKE_BINARY_DIR}/fake_folly")
  file(MAKE_DIRECTORY "${folly_DIR}")
  file(WRITE "${folly_DIR}/follyConfig.cmake" "set(folly_FOUND TRUE)")
  list(PREPEND CMAKE_MODULE_PATH "${folly_DIR}")
  add_library(Folly::folly ALIAS dwarfs_folly_lite)
  set(CMAKE_SKIP_INSTALL_RULES ON)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fbthrift EXCLUDE_FROM_ALL SYSTEM)
  unset(CMAKE_SKIP_INSTALL_RULES)
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
else()
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_cpp2_thrift_library(fbthrift/thrift/lib/thrift/frozen.thrift
                        OUTPUT_PATH lib/thrift NO_LIBRARY)

add_library(
  dwarfs_thrift_lite OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp/protocol/TBase64Utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp/protocol/TProtocolException.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp/util/VarintUtils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/FieldRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/Frozen.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/FrozenUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/frozen/schema/MemorySchema.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/gen/module_types_cpp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/BinaryProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/CompactProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/DebugProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/JSONProtocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/JSONProtocolCommon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/Protocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/TableBasedSerializer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/TableBasedSerializerImpl.cpp
  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp2/frozen_data.cpp
  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.cpp
  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types_compact.cpp
)

set_property(TARGET dwarfs_thrift_lite PROPERTY CXX_STANDARD ${DWARFS_CXX_STANDARD})
target_link_libraries(dwarfs_thrift_lite PUBLIC dwarfs_folly_lite)

target_include_directories(dwarfs_thrift_lite SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fbthrift>
  $<BUILD_INTERFACE:${THRIFT_GENERATED_DIR}>
)

