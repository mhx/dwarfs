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
set(FOLLY_USE_JEMALLOC ${USE_JEMALLOC})

# TODO: this is due to a bug in folly's Portability.h
add_compile_definitions(FOLLY_CFG_NO_COROUTINES)

add_compile_definitions(GLOG_NO_ABBREVIATED_SEVERITIES NOMINMAX NOGDI)

# TODO: temporary workaround until this is fixed in folly
#       see https://github.com/facebook/folly/issues/2149
add_compile_definitions(GLOG_USE_GLOG_EXPORT)

set(
  CXX_STD "gnu++20"
  CACHE STRING
  "The C++ standard argument to pass to the compiler."
)

set(
  MSVC_LANGUAGE_VERSION "c++20"
  CACHE STRING
  "The C++ standard argument to pass to the compiler."
)

set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON)
set(CMAKE_DISABLE_FIND_PACKAGE_Snappy ON)
set(CMAKE_DISABLE_FIND_PACKAGE_LibAIO ON)
set(CMAKE_DISABLE_FIND_PACKAGE_LibUring ON)
set(CMAKE_DISABLE_FIND_PACKAGE_Libsodium ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/folly EXCLUDE_FROM_ALL SYSTEM)

if(NOT DWARFS_FMT_LIB)
  get_target_property(FOLLY_DEPS_INTERFACE_LINK_LIBRARIES folly_deps INTERFACE_LINK_LIBRARIES)
  list(REMOVE_ITEM FOLLY_DEPS_INTERFACE_LINK_LIBRARIES fmt::fmt)
  set_target_properties(folly_deps PROPERTIES INTERFACE_LINK_LIBRARIES "${FOLLY_DEPS_INTERFACE_LINK_LIBRARIES}")
endif()

add_library(dwarfs_folly_lite OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Conv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Demangle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ExceptionString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/File.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/FileUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ScopeGuard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/String.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/container/detail/F14Table.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/FileUtilDetail.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/SplitStringSimd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/hash/SpookyHashV2.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBuf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBufQueue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/CString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/Exception.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/SafeAssert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/ToAscii.cpp
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

if(ENABLE_STACKTRACE)
  target_sources(dwarfs_folly_lite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/Dwarf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/DwarfImpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/DwarfLineNumberVM.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/DwarfSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/DwarfUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/Elf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/ElfCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/LineReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/SignalHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/StackTrace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/SymbolizePrinter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/SymbolizedFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/debugging/symbolizer/Symbolizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/tracing/AsyncStack.cpp
  )
endif()

set_property(TARGET dwarfs_folly_lite PROPERTY CXX_STANDARD 20)
target_include_directories(
  dwarfs_folly_lite SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/folly>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/folly>
)
apply_folly_compile_options_to_target(dwarfs_folly_lite)
target_link_libraries(dwarfs_folly_lite PUBLIC folly_deps)

if(WITH_BENCHMARKS)
  add_library(dwarfs_follybenchmark_lite OBJECT 
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Benchmark.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Format.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Unicode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/PerfScoped.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/StaticSingletonManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ext/test_ext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/FsUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/dynamic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/json/json_pointer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/AsyncFileWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/AsyncLogWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/CustomLogFormatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/FileWriterFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/GlogStyleFormatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/ImmediateFileWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogCategory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogCategoryConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogHandlerConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogLevel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogMessage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogStream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LogStreamProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/LoggerDB.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/ObjectToString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/RateLimiter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/StandardLogHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/StandardLogHandlerFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/StreamHandlerFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/logging/xlog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/Pid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/testing/TestUtil.cpp
  )
  if(NOT WIN32)
    target_sources(dwarfs_follybenchmark_lite PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Subprocess.cpp
    )
  endif()
  set_property(TARGET dwarfs_follybenchmark_lite PROPERTY CXX_STANDARD 20)
  apply_folly_compile_options_to_target(dwarfs_follybenchmark_lite)
  target_link_libraries(dwarfs_follybenchmark_lite PUBLIC dwarfs_folly_lite)
endif()

if(ENABLE_STACKTRACE OR WITH_BENCHMARKS)
  if(ENABLE_STACKTRACE)
    set(_target dwarfs_folly_lite)
  else()
    set(_target dwarfs_follybenchmark_lite)
  endif()
  target_sources(${_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/SharedMutex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/concurrency/CacheLocality.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/Futex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/ReentrantAllocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/synchronization/ParkingLot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/synchronization/SanitizeThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/AtFork.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/ThreadId.cpp
  )
endif()
