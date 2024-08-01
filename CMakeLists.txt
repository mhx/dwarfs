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

# Enable CMAKE_MSVC_RUNTIME_LIBRARY
cmake_policy(SET CMP0091 NEW)

project(dwarfs)

include(ExternalProject)

include(CheckCXXSourceCompiles)

option(WITH_TESTS "build with tests" OFF)
option(WITH_BENCHMARKS "build with benchmarks" OFF)
option(WITH_FUZZ "build with fuzzing binaries" OFF)
option(WITH_MAN_OPTION "build with --man option" ON)
option(ENABLE_PERFMON "enable performance monitor in all tools" ON)
option(ENABLE_FLAC "build with FLAC support" ON)
option(ENABLE_RICEPP "build with RICEPP compression support" ON)
option(WITH_UNIVERSAL_BINARY "build with universal binary" ON)
option(WITH_PXATTR "build with pxattr binary" OFF)
if(APPLE)
  option(USE_HOMEBREW_LIBARCHIVE "use libarchive from homebrew" ON)
endif()
if(WIN32)
  option(USE_RICEPP_PACKAGE "use pre-built RICEPP package" OFF)
else()
  option(WITH_LEGACY_FUSE "build fuse2 driver even if we have fuse3" OFF)
  option(WITH_MAN_PAGES "build man pages using ronn" ON)
  option(ENABLE_ASAN "enable address sanitizer" OFF)
  option(ENABLE_TSAN "enable thread sanitizer" OFF)
  option(ENABLE_UBSAN "enable undefined behaviour sanitizer" OFF)
  option(ENABLE_COVERAGE "enable code coverage" OFF)
  if(APPLE)
    # This doesn't work reliably on macOS at the moment
    set(USE_JEMALLOC OFF)
  else()
    option(USE_JEMALLOC "build with jemalloc" ON)
  endif()
  option(PREFER_SYSTEM_GTEST "use system gtest if available" OFF)
  option(DISABLE_CCACHE "disable ccache" OFF)
  option(DISABLE_MOLD "disable mold" OFF)
  option(USE_PREFERRED_LIBS "use preferred versions of libraries" OFF)
  option(STATIC_BUILD_DO_NOT_USE "try static build (experimental)" OFF)
endif()

set(LIBFMT_REQUIRED_VERSION 10.0)
set(LIBFMT_PREFERRED_VERSION 10.2.1)

set(GOOGLETEST_REQUIRED_VERSION 1.13.0)
set(GOOGLETEST_PREFERRED_VERSION 1.14.0)

set(RANGE_V3_REQUIRED_VERSION 0.12.0)
set(RANGE_V3_PREFERRED_VERSION 0.12.0)

set(PARALLEL_HASHMAP_REQUIRED_VERSION 1.3.8)
set(PARALLEL_HASHMAP_PREFERRED_VERSION 1.3.12)

set(XXHASH_REQUIRED_VERSION 0.8.1)
set(XXHASH_PREFERRED_VERSION 0.8.2)

set(ZSTD_REQUIRED_VERSION 1.5.2)
set(ZSTD_PREFERRED_VERSION 1.5.6)

if(DEFINED ENV{DWARFS_LOCAL_REPO_PATH})
  set(LIBFMT_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/fmt)
  set(GOOGLETEST_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/googletest)
  set(RANGE_V3_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/range-v3)
  set(PARALLEL_HASHMAP_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/parallel-hashmap)
  set(XXHASH_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/xxHash)
  set(ZSTD_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/zstd)
else()
  set(LIBFMT_GIT_REPO https://github.com/fmtlib/fmt.git)
  set(GOOGLETEST_GIT_REPO https://github.com/google/googletest.git)
  set(RANGE_V3_GIT_REPO https://github.com/ericniebler/range-v3.git)
  set(PARALLEL_HASHMAP_GIT_REPO https://github.com/greg7mdp/parallel-hashmap.git)
  set(XXHASH_GIT_REPO https://github.com/Cyan4973/xxHash.git)
  set(ZSTD_GIT_REPO https://github.com/facebook/zstd.git)
endif()

if(APPLE)
  set(HOMEBREW_PREFIX_PATH "/opt/homebrew")
  set(TRY_RUN_INCLUDE_DIRECTORIES ${HOMEBREW_PREFIX_PATH}/include)
endif()

if(BUILD_SHARED_LIBS)
  message(WARNING "Building shared libraries is unsupported. Setting BUILD_SHARED_LIBS to OFF.")
endif()

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT DISABLE_CCACHE)
  find_program(CCACHE_EXE ccache ccache.exe PATHS "c:/bin")
  if(CCACHE_EXE)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXE})
    if(DEFINED ENV{GITHUB_REF_TYPE})
      message(STATUS "Using ccache from ${CCACHE_EXE}")
      execute_process(COMMAND ${CCACHE_EXE} -p OUTPUT_VARIABLE CCACHE_CONFIG ERROR_QUIET)
      message(STATUS "ccache config:\n${CCACHE_CONFIG}")
    endif()
  endif()
endif()

if(DEFINED ENV{CCACHE_PREFIX})
  add_compile_options(-Wno-gnu-line-marker)
endif()

if(NOT WIN32)
  if(NOT DISABLE_MOLD)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      find_program(MOLD_EXE mold)
      if(MOLD_EXE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${MOLD_EXE}")
      endif()
    endif()
  endif()
endif()

set(default_build_type "Release")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}'")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Build Type" FORCE)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-fdiagnostics-color=always)
  # For gcc, -O3 is *much* worse than -O2
  # Update: This is still true for gcc-12
  # set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -g")
  # set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -g")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_compile_options(-fcolor-diagnostics)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/Zc:__cplusplus /utf-8 /wd4267 /wd4244 /wd5219)
  # Apply /MT or /MTd  (multithread, static version of the run-time library)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
  add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/version.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/thrift_library.cmake)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules")

if(STATIC_BUILD_DO_NOT_USE)
  # Strangely this is needed when linking statically against FLAC++
  #add_compile_options(-fno-omit-frame-pointer)

  if(STATIC_BUILD_EXTRA_PREFIX)
    include_directories(BEFORE ${STATIC_BUILD_EXTRA_PREFIX}/include)
    list(PREPEND CMAKE_PREFIX_PATH ${STATIC_BUILD_EXTRA_PREFIX})
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
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
  set(FOLLY_NO_EXCEPTION_TRACER
      ON
      CACHE BOOL "disable exception tracer")
endif()

if(WITH_MAN_PAGES)
  find_program(RONN_EXE ronn DOC "ronn man page generator" REQUIRED)

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
      message(WARNING "${RONN_EXE} failed to process ${man_input} -> ${man}")
      message(WARNING "error: ${ronn_error}")
    endif()
  endforeach()
endif()

if(NOT USE_PREFERRED_LIBS)
  find_package(fmt ${LIBFMT_REQUIRED_VERSION} CONFIG QUIET)
endif()

include(FetchContent)

if(NOT fmt_FOUND)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY ${LIBFMT_GIT_REPO}
    GIT_TAG ${LIBFMT_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(fmt)
endif()

if(NOT USE_PREFERRED_LIBS)
  find_package(range-v3 ${RANGE_V3_REQUIRED_VERSION} CONFIG QUIET)
endif()

if(NOT range-v3_FOUND)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY ${RANGE_V3_GIT_REPO}
    GIT_TAG ${RANGE_V3_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(range-v3)
endif()

if(NOT USE_PREFERRED_LIBS)
  try_run(
    PHMAP_RUN_RESULT
    PHMAP_COMPILE_RESULT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/phmap-version.cpp
    CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${TRY_RUN_INCLUDE_DIRECTORIES}
    CXX_STANDARD 20
    RUN_OUTPUT_VARIABLE PHMAP_VERSION
    COMPILE_OUTPUT_VARIABLE PHMAP_COMPILE_OUTPUT
  )
endif()

if(PHMAP_RUN_RESULT EQUAL 0)
  if(PHMAP_VERSION VERSION_LESS ${PARALLEL_HASHMAP_REQUIRED_VERSION})
    string(STRIP "${PHMAP_VERSION}" PHMAP_VERSION)
    message(STATUS "System-installed parallel-hashmap version ${PHMAP_VERSION} is less than required version ${PARALLEL_HASHMAP_REQUIRED_VERSION}")
  endif()
else()
  message(STATUS "failed to check parallel-hashmap version")
  message(VERBOSE "${PHMAP_COMPILE_OUTPUT}")
endif()

if(PHMAP_RUN_RESULT EQUAL 0 AND PHMAP_VERSION VERSION_GREATER_EQUAL ${PARALLEL_HASHMAP_REQUIRED_VERSION})
  add_library(phmap INTERFACE)
else()
  FetchContent_Declare(
    parallel-hashmap
    GIT_REPOSITORY ${PARALLEL_HASHMAP_GIT_REPO}
    GIT_TAG v${PARALLEL_HASHMAP_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(parallel-hashmap)
endif()

find_package(Boost 1.67 REQUIRED COMPONENTS chrono iostreams program_options)

if(STATIC_BUILD_DO_NOT_USE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_package(PkgConfig)

if(APPLE)
  # For whatever reason, thrift is unhappy if we don't do this
  find_package(OpenSSL 1.1.1 MODULE REQUIRED)

  if(USE_HOMEBREW_LIBARCHIVE)
    find_program(HOMEBREW_EXE brew)
    execute_process(
      COMMAND ${HOMEBREW_EXE} --prefix libarchive
      OUTPUT_VARIABLE LIBARCHIVE_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    list(PREPEND CMAKE_PREFIX_PATH ${LIBARCHIVE_PREFIX})
  endif()
endif()

if(STATIC_BUILD_DO_NOT_USE)
  list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto>=3.0.0)
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=1.9.3)
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=5.2.5)
pkg_check_modules(LIBBROTLIDEC IMPORTED_TARGET libbrotlidec>=1.0.9)
pkg_check_modules(LIBBROTLIENC IMPORTED_TARGET libbrotlienc>=1.0.9)
pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive>=3.6.0)
# pkg_check_modules(LIBMAGIC IMPORTED_TARGET libmagic>=5.38)
if(ENABLE_FLAC)
  pkg_check_modules(FLAC IMPORTED_TARGET flac++>=1.4.2)
endif()
if(NOT USE_PREFERRED_LIBS)
  pkg_check_modules(ZSTD IMPORTED_TARGET libzstd>=${ZSTD_REQUIRED_VERSION})
  pkg_check_modules(XXHASH IMPORTED_TARGET libxxhash>=${XXHASH_REQUIRED_VERSION})
endif()

if(WIN32)
  if(NOT WINFSP_PATH)
    set(WINFSP_PATH "C:/Program Files (x86)/WinFsp")
  endif()
  find_library(WINFSP winfsp-x64.lib "${WINFSP_PATH}/lib")
  if (NOT WINFSP)
    message(FATAL_ERROR "No WinFsp library found")
  endif()
else()
  pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.9.9)
  pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3>=3.10.5)

  if(NOT FUSE_FOUND AND NOT FUSE3_FOUND)
    message(FATAL_ERROR "No FUSE or FUSE3 library found")
  endif()
endif()

if(XXHASH_FOUND)
  add_library(xxHash::xxhash ALIAS PkgConfig::XXHASH)
else()
  FetchContent_Declare(
    libxxhash
    GIT_REPOSITORY ${XXHASH_GIT_REPO}
    GIT_TAG v${XXHASH_PREFERRED_VERSION}
    SOURCE_SUBDIR cmake_unofficial
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(libxxhash)
endif()

if(ZSTD_FOUND)
  add_library(libzstd_static ALIAS PkgConfig::ZSTD)
else()
  FetchContent_Declare(
    zstd
    GIT_REPOSITORY ${ZSTD_GIT_REPO}
    GIT_TAG v${ZSTD_PREFERRED_VERSION}
    SOURCE_SUBDIR build/cmake
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(zstd)
endif()

if(USE_JEMALLOC)
  pkg_check_modules(JEMALLOC IMPORTED_TARGET jemalloc>=5.2.1)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  foreach(CompilerFlag CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REPLACE "/RTC1" "" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()

  set(MSVC_USE_STATIC_RUNTIME ON CACHE BOOL "static build")
endif()

set(ZSTD_INCLUDE_DIR
    ""
    CACHE PATH "don't build folly with zstd" FORCE)

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

add_subdirectory(folly EXCLUDE_FROM_ALL SYSTEM)

if(DWARFS_GIT_BUILD)
  set(THRIFT_COMPILER_ONLY
      ON
      CACHE BOOL "only build thrift compiler")

  add_subdirectory(fbthrift EXCLUDE_FROM_ALL SYSTEM)
endif()

add_library(dwarfs_folly_lite
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Conv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Demangle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ExceptionString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/File.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/FileUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ScopeGuard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/SharedMutex.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/String.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/concurrency/CacheLocality.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/container/detail/F14Table.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/FileUtilDetail.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/Futex.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/RangeCommon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/RangeSse42.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/SimpleSimdStringUtils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/SplitStringSimd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/ext/test_ext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/hash/SpookyHashV2.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/FsUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBuf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/io/IOBufQueue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/CString.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/Exception.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/SafeAssert.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/lang/ToAscii.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/ReentrantAllocator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/SanitizeAddress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/SanitizeLeak.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/memory/detail/MallocImpl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/net/NetOps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/net/detail/SocketFileDescriptorMap.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Fcntl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/PThread.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Sockets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Stdlib.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysMman.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysResource.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysStat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/SysUio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Time.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/portability/Unistd.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/QuantileEstimator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/TDigest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/stats/detail/DoubleRadixSort.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/synchronization/ParkingLot.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/synchronization/SanitizeThread.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/AtFork.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/HardwareConcurrency.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/ThreadId.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/system/ThreadName.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/testing/TestUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/tracing/AsyncStack.cpp
)

list(APPEND FOLLY_BENCHMARK_LITE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Benchmark.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Unicode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/PerfScoped.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/detail/StaticSingletonManager.cpp
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
)

if(NOT WIN32)
  list(APPEND FOLLY_BENCHMARK_LITE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly/Subprocess.cpp
  )
endif()

set_property(TARGET dwarfs_folly_lite PROPERTY CXX_STANDARD 20)
target_include_directories(
  dwarfs_folly_lite SYSTEM PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}/folly
  ${CMAKE_CURRENT_SOURCE_DIR}/folly
)
apply_folly_compile_options_to_target(dwarfs_folly_lite)
target_link_libraries(dwarfs_folly_lite PUBLIC folly_deps)

set(ZSTD_LIBRARY_RELEASE
    "ZSTD_LIBRARY_RELEASE-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_DEBUG
    "ZSTD_LIBRARY_DEBUG-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)

if(WITH_TESTS)
  if(PREFER_SYSTEM_GTEST)
    find_package(GTest ${GOOGLETEST_REQUIRED_VERSION} CONFIG)
    add_library(gtest ALIAS GTest::gtest)
    add_library(gtest_main ALIAS GTest::gtest_main)
    add_library(gmock ALIAS GTest::gmock)
    add_library(gmock_main ALIAS GTest::gmock_main)
  endif()

  if(NOT GTest_FOUND)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY ${GOOGLETEST_GIT_REPO}
      GIT_TAG v${GOOGLETEST_PREFERRED_VERSION}
      EXCLUDE_FROM_ALL
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  include(GoogleTest)
endif()

if(DWARFS_OPTIMIZE)
  string(REPLACE "-O3" "-O${DWARFS_OPTIMIZE}" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REPLACE "-O3" "-O${DWARFS_OPTIMIZE}" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

if(ENABLE_RICEPP)
  if(USE_RICEPP_PACKAGE)
    find_package(ricepp)
    add_library(ricepp ALIAS ricepp::ricepp)
  else()
    add_subdirectory(ricepp)
  endif()
endif()

list(APPEND LIBDWARFS_COMMON_SRC
  src/dwarfs/block_compressor.cpp
  src/dwarfs/block_compressor_parser.cpp
  src/dwarfs/checksum.cpp
  src/dwarfs/compression_metadata_requirements.cpp
  src/dwarfs/conv.cpp
  src/dwarfs/error.cpp
  src/dwarfs/file_access_generic.cpp
  src/dwarfs/file_stat.cpp
  src/dwarfs/file_type.cpp
  src/dwarfs/fstypes.cpp
  src/dwarfs/history.cpp
  src/dwarfs/internal/features.cpp
  src/dwarfs/internal/string_table.cpp
  src/dwarfs/internal/wcwidth.c
  src/dwarfs/internal/worker_group.cpp
  src/dwarfs/library_dependencies.cpp
  src/dwarfs/logger.cpp
  src/dwarfs/mmap.cpp
  src/dwarfs/option_map.cpp
  src/dwarfs/options.cpp
  src/dwarfs/os_access_generic.cpp
  src/dwarfs/pcm_sample_transformer.cpp
  src/dwarfs/performance_monitor.cpp
  src/dwarfs/terminal.cpp
  src/dwarfs/thread_pool.cpp
  src/dwarfs/util.cpp
  src/dwarfs/xattr.cpp
)

list(APPEND LIBDWARFS_READER_SRC
  src/dwarfs/block_range.cpp
  src/dwarfs/filesystem_v2.cpp
  src/dwarfs/internal/block_cache.cpp
  src/dwarfs/internal/cached_block.cpp
  src/dwarfs/internal/fs_section.cpp
  src/dwarfs/internal/inode_reader_v2.cpp
  src/dwarfs/internal/metadata_types.cpp
  src/dwarfs/internal/metadata_v2.cpp
  src/dwarfs/metadata_types.cpp
)

list(APPEND LIBDWARFS_WRITER_SRC
  src/dwarfs/builtin_script.cpp
  src/dwarfs/categorizer.cpp
  src/dwarfs/category_parser.cpp
  src/dwarfs/chmod_entry_transformer.cpp
  src/dwarfs/console_writer.cpp
  src/dwarfs/entry_factory.cpp
  src/dwarfs/filesystem_block_category_resolver.cpp
  src/dwarfs/filesystem_writer.cpp
  src/dwarfs/filter_debug.cpp
  src/dwarfs/fragment_category.cpp
  src/dwarfs/fragment_order_parser.cpp
  src/dwarfs/inode_fragments.cpp
  src/dwarfs/internal/block_manager.cpp
  src/dwarfs/internal/chmod_transformer.cpp
  src/dwarfs/internal/entry.cpp
  src/dwarfs/internal/file_scanner.cpp
  src/dwarfs/internal/fragment_chunkable.cpp
  src/dwarfs/internal/global_entry_data.cpp
  src/dwarfs/internal/inode_element_view.cpp
  src/dwarfs/internal/inode_manager.cpp
  src/dwarfs/internal/inode_ordering.cpp
  src/dwarfs/internal/metadata_freezer.cpp
  src/dwarfs/internal/nilsimsa.cpp
  src/dwarfs/internal/progress.cpp
  src/dwarfs/internal/scanner_progress.cpp
  src/dwarfs/internal/similarity.cpp
  src/dwarfs/internal/similarity_ordering.cpp
  src/dwarfs/scanner.cpp
  src/dwarfs/segmenter.cpp
  src/dwarfs/segmenter_factory.cpp
  src/dwarfs/writer_progress.cpp
)

list(APPEND LIBDWARFS_EXTRACTOR_SRC
  src/dwarfs/filesystem_extractor.cpp
)

list(APPEND LIBDWARFS_TOOL_SRC
  src/dwarfs/iolayer.cpp
  src/dwarfs/safe_main.cpp
  src/dwarfs/tool.cpp
)

if(WIN32)
  list(APPEND LIBDWARFS_COMMON_SRC src/dwarfs/xattr_win.cpp)
else()
  list(APPEND LIBDWARFS_COMMON_SRC src/dwarfs/xattr_posix.cpp)
endif()

if(WITH_MAN_OPTION)
  list(APPEND LIBDWARFS_COMMON_SRC
    src/dwarfs/pager.cpp
    src/dwarfs/render_manpage.cpp
  )

  if(DWARFS_GIT_BUILD)
    include(${CMAKE_SOURCE_DIR}/cmake/render_manpage.cmake)
    set(DWARFS_MANPAGE_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(DWARFS_MANPAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  foreach(man mkdwarfs dwarfs dwarfsck dwarfsextract)
    if(DWARFS_GIT_BUILD)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs")
      add_manpage_source(doc/${man}.md NAME ${man}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs/${man}_manpage.cpp)
    endif()

    # TODO: this doesn't need to be in the common library -> just link to the
    #       specific tool
    list(APPEND LIBDWARFS_COMMON_SRC ${DWARFS_MANPAGE_SOURCE_DIR}/src/dwarfs/${man}_manpage.cpp)
  endforeach()
endif()

# Just an example for setting per file compile options
# set_source_files_properties(src/dwarfs/segmenter.cpp PROPERTIES COMPILE_FLAGS -march=tigerlake)

if(DWARFS_GIT_BUILD)
  list(APPEND LIBDWARFS_COMMON_SRC ${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs/version.cpp)
else()
  list(APPEND LIBDWARFS_COMMON_SRC src/dwarfs/version.cpp)
endif()

list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/null.cpp)
list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/zstd.cpp)

if(LIBLZMA_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/lzma.cpp)
endif()

if(LIBLZ4_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/lz4.cpp)
endif()

if(LIBBROTLIDEC_FOUND AND LIBBROTLIENC_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/brotli.cpp)
endif()

if(FLAC_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/flac.cpp)
endif()

list(
  APPEND
  LIBDWARFS_CATEGORIZER_SRC
  # src/dwarfs/categorizer/binary_categorizer.cpp
  src/dwarfs/categorizer/fits_categorizer.cpp
  src/dwarfs/categorizer/incompressible_categorizer.cpp
  src/dwarfs/categorizer/pcmaudio_categorizer.cpp
)

if(ENABLE_RICEPP)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/ricepp.cpp)
endif()

# if(LIBMAGIC_FOUND)
#   list(APPEND LIBDWARFS_CATEGORIZER_SRC src/dwarfs/categorizer/libmagic_categorizer.cpp)
# endif()

add_library(dwarfs_common ${LIBDWARFS_COMMON_SRC} ${LIBDWARFS_COMPRESSION_SRC})
add_library(dwarfs_reader ${LIBDWARFS_READER_SRC})
add_library(dwarfs_writer ${LIBDWARFS_WRITER_SRC} ${LIBDWARFS_CATEGORIZER_SRC})
add_library(dwarfs_extractor ${LIBDWARFS_EXTRACTOR_SRC})
add_library(dwarfs_tool OBJECT ${LIBDWARFS_TOOL_SRC})

target_link_libraries(dwarfs_common PUBLIC dwarfs_folly_lite)
target_link_libraries(dwarfs_common PRIVATE PkgConfig::LIBCRYPTO xxHash::xxhash phmap range-v3::range-v3)
target_link_libraries(dwarfs_reader PUBLIC dwarfs_common)
target_link_libraries(dwarfs_reader PRIVATE range-v3::range-v3)
target_link_libraries(dwarfs_writer PUBLIC dwarfs_common)
target_link_libraries(dwarfs_writer PRIVATE phmap range-v3::range-v3)
target_link_libraries(dwarfs_extractor PUBLIC dwarfs_reader)
target_link_libraries(dwarfs_tool PUBLIC dwarfs_common)

if(DWARFS_GIT_BUILD)
  target_include_directories(dwarfs_common PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
endif()

target_compile_definitions(
  dwarfs_tool PRIVATE PRJ_BUILD_ID="${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SYSTEM}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

target_compile_definitions(
  dwarfs_common PRIVATE
  PRJ_SYSTEM_ID="${CMAKE_SYSTEM} [${CMAKE_SYSTEM_PROCESSOR}]"
  PRJ_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

if(ENABLE_RICEPP)
  target_link_libraries(dwarfs_common PRIVATE ricepp)
endif()
target_link_libraries(dwarfs_tool PUBLIC dwarfs_common)

if(STATIC_BUILD_DO_NOT_USE)
  add_link_options(-static -static-libgcc)
endif(STATIC_BUILD_DO_NOT_USE)

add_library(mkdwarfs_main OBJECT src/mkdwarfs_main.cpp)
add_library(dwarfsck_main OBJECT src/dwarfsck_main.cpp)
add_library(dwarfsbench_main OBJECT src/dwarfsbench_main.cpp)
add_library(dwarfsextract_main OBJECT src/dwarfsextract_main.cpp)

add_executable(mkdwarfs src/mkdwarfs.cpp)
add_executable(dwarfsck src/dwarfsck.cpp)
add_executable(dwarfsbench src/dwarfsbench.cpp)
add_executable(dwarfsextract src/dwarfsextract.cpp)
list(APPEND BINARY_TARGETS mkdwarfs dwarfsck dwarfsbench dwarfsextract)

if(WITH_UNIVERSAL_BINARY)
  add_executable(dwarfsuniversal src/universal.cpp)
  list(APPEND BINARY_TARGETS dwarfsuniversal)

  target_link_libraries(dwarfsuniversal PRIVATE
          mkdwarfs_main dwarfsck_main dwarfsextract_main range-v3::range-v3)
  set_target_properties(dwarfsuniversal PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY universal
          OUTPUT_NAME dwarfs-universal)
endif()

if(WITH_PXATTR)
  add_executable(pxattr src/pxattr.cpp)
  list(APPEND BINARY_TARGETS pxattr)
endif()

install(TARGETS mkdwarfs dwarfsck dwarfsbench dwarfsextract RUNTIME DESTINATION bin)

list(APPEND MAIN_TARGETS mkdwarfs_main dwarfsck_main dwarfsbench_main
        dwarfsextract_main)

if(FUSE3_FOUND OR WINFSP OR APPLE)
  add_library(dwarfs_main OBJECT src/dwarfs_main.cpp)
  target_compile_definitions(dwarfs_main PRIVATE _FILE_OFFSET_BITS=64)
  add_executable(dwarfs-bin src/dwarfs.cpp)
  target_link_libraries(dwarfs-bin PRIVATE dwarfs_main)
  set_target_properties(dwarfs-bin PROPERTIES OUTPUT_NAME dwarfs)
  if(WINFSP)
    target_compile_definitions(dwarfs_main PRIVATE FUSE_USE_VERSION=32
                                                   DWARFS_FUSE_LOWLEVEL=0)
    target_include_directories(dwarfs_main SYSTEM PRIVATE "${WINFSP_PATH}/inc")
    target_link_libraries(dwarfs_main PRIVATE ${WINFSP})
    target_link_libraries(dwarfs-bin PRIVATE delayimp.lib)
    target_link_options(dwarfs-bin PRIVATE /DELAYLOAD:winfsp-x64.dll)
    if(WITH_UNIVERSAL_BINARY)
      target_link_libraries(dwarfsuniversal PRIVATE delayimp.lib)
      target_link_options(dwarfsuniversal PRIVATE /DELAYLOAD:winfsp-x64.dll)
    endif()
  elseif(APPLE)
    target_compile_definitions(dwarfs_main PRIVATE FUSE_USE_VERSION=29)
    target_link_libraries(dwarfs_main PRIVATE PkgConfig::FUSE)
  else()
    target_compile_definitions(dwarfs_main PRIVATE FUSE_USE_VERSION=35)
    target_link_libraries(dwarfs_main PRIVATE PkgConfig::FUSE3)
  endif()
  if(WITH_UNIVERSAL_BINARY)
    target_link_libraries(dwarfsuniversal PRIVATE dwarfs_main)
  endif()
  if(WINFSP)
    install(TARGETS dwarfs-bin RUNTIME DESTINATION bin)
  else()
    add_custom_command(OUTPUT  mount.dwarfs
                       COMMAND ${CMAKE_COMMAND} -E create_symlink dwarfs mount.dwarfs
                       DEPENDS dwarfs-bin)
    list(APPEND SYMLINKS mount.dwarfs)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs DESTINATION sbin)
    install(TARGETS dwarfs-bin RUNTIME DESTINATION sbin)
  endif()
  list(APPEND BINARY_TARGETS dwarfs-bin)
  list(APPEND MAIN_TARGETS dwarfs_main)
endif()

if(FUSE_FOUND AND (NOT APPLE) AND (WITH_LEGACY_FUSE OR NOT FUSE3_FOUND))
  add_library(dwarfs2_main src/dwarfs_main.cpp)
  target_compile_definitions(dwarfs2_main PRIVATE _FILE_OFFSET_BITS=64
                                                  FUSE_USE_VERSION=29)
  target_link_libraries(dwarfs2_main PRIVATE PkgConfig::FUSE)
  add_executable(dwarfs2-bin src/dwarfs.cpp)
  target_link_libraries(dwarfs2-bin PRIVATE dwarfs2_main)
  if(WITH_UNIVERSAL_BINARY AND (NOT FUSE3_FOUND))
    target_link_libraries(dwarfsuniversal PRIVATE dwarfs2_main)
  endif()
  set_target_properties(dwarfs2-bin PROPERTIES OUTPUT_NAME dwarfs2)
  add_custom_command(OUTPUT  mount.dwarfs2
                     COMMAND ${CMAKE_COMMAND} -E create_symlink dwarfs2 mount.dwarfs2
                     DEPENDS dwarfs2-bin)
  list(APPEND SYMLINKS mount.dwarfs2)
  install(TARGETS dwarfs2-bin RUNTIME DESTINATION sbin)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs2 DESTINATION sbin)
  list(APPEND BINARY_TARGETS dwarfs2-bin)
  list(APPEND MAIN_TARGETS dwarfs2_main)
endif()

add_custom_target(symlinks ALL DEPENDS ${SYMLINKS})

if(WITH_TESTS OR WITH_BENCHMARKS OR WITH_FUZZ)
  add_library(dwarfs_test_helpers OBJECT
    test/test_helpers.cpp
    test/test_iolayer.cpp
    test/test_strings.cpp
    test/loremipsum.cpp
    test/test_dirtree.cpp
    test/filter_test_data.cpp
  )
  target_link_libraries(dwarfs_test_helpers PUBLIC dwarfs_common)
  set_property(TARGET dwarfs_test_helpers PROPERTY CXX_STANDARD 20)
endif()

if(WITH_TESTS)
  list(APPEND DWARFS_TESTS
    badfs_test
    block_cache_test
    block_merger_test
    checksum_test
    chmod_transformer_test
    compat_test
    dwarfs_test
    entry_test
    error_test
    file_access_test
    filesystem_test
    fits_categorizer_test
    fragment_category_test
    incompressible_categorizer_test
    integral_value_parser_test
    lazy_value_test
    metadata_requirements_test
    options_test
    pcm_sample_transformer_test
    pcmaudio_categorizer_test
    speedometer_test
    terminal_test
    tool_main_test
    tools_test
    utils_test
    worker_group_test
  )

  if(WITH_MAN_OPTION)
    list(APPEND DWARFS_TESTS manpage_test)
  endif()

  if(FLAC_FOUND)
    list(APPEND DWARFS_TESTS flac_compressor_test)
  endif()

  if(ENABLE_RICEPP)
    list(APPEND DWARFS_TESTS ricepp_compressor_test)
  endif()

  foreach (test ${DWARFS_TESTS})
    add_executable(${test} test/${test}.cpp)
    target_link_libraries(
      ${test} PRIVATE dwarfs_test_helpers gmock gtest gtest_main
    )
    target_compile_definitions(${test}
      PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\"
              TOOLS_BIN_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\")
    list(APPEND TEST_TARGETS ${test})
  endforeach()

  set_source_files_properties(test/tools_test.cpp PROPERTIES
    COMPILE_DEFINITIONS
    $<$<BOOL:${WITH_UNIVERSAL_BINARY}>:DWARFS_HAVE_UNIVERSAL_BINARY>
  )

  target_link_libraries(tool_main_test PRIVATE
    mkdwarfs_main dwarfsck_main dwarfsbench_main dwarfsextract_main range-v3::range-v3)

  target_link_libraries(block_cache_test PRIVATE mkdwarfs_main range-v3::range-v3)
  target_link_libraries(block_merger_test PRIVATE range-v3::range-v3)
  target_link_libraries(chmod_transformer_test PRIVATE range-v3::range-v3)
  target_link_libraries(ricepp_compressor_test PRIVATE range-v3::range-v3)

  foreach(tgt fits_categorizer_test
              incompressible_categorizer_test
              pcmaudio_categorizer_test
              tool_main_test)
    target_link_libraries(${tgt} PRIVATE dwarfs_writer)
  endforeach()

  foreach(tgt ${TEST_TARGETS})
    gtest_discover_tests(${tgt}
      DISCOVERY_TIMEOUT 120
      PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profile/%32m.profraw"
    )
  endforeach()
endif()

if(WITH_BENCHMARKS)
  find_package(benchmark 1.8)
  if(benchmark_FOUND)
    add_executable(dwarfs_benchmark test/dwarfs_benchmark.cpp)
    target_link_libraries(dwarfs_benchmark PRIVATE dwarfs_test_helpers benchmark::benchmark)
    list(APPEND BINARY_TARGETS dwarfs_benchmark)

    add_executable(multiversioning_benchmark test/multiversioning_benchmark.cpp)
    target_link_libraries(multiversioning_benchmark PRIVATE benchmark::benchmark)
    list(APPEND BINARY_TARGETS multiversioning_benchmark)

    add_executable(converter_benchmark test/converter_benchmark.cpp)
    target_link_libraries(converter_benchmark PRIVATE dwarfs_test_helpers benchmark::benchmark)
    list(APPEND BINARY_TARGETS converter_benchmark)
  endif()

  add_library(dwarfs_follybenchmark_lite OBJECT ${FOLLY_BENCHMARK_LITE_SRC})
  set_property(TARGET dwarfs_follybenchmark_lite PROPERTY CXX_STANDARD 20)
  apply_folly_compile_options_to_target(dwarfs_follybenchmark_lite)
  target_link_libraries(dwarfs_follybenchmark_lite PUBLIC dwarfs_folly_lite)

  # TODO: migrate to benchmark?
  add_executable(segmenter_benchmark test/segmenter_benchmark.cpp)
  target_link_libraries(segmenter_benchmark PRIVATE dwarfs_follybenchmark_lite dwarfs_test_helpers)
  list(APPEND BINARY_TARGETS segmenter_benchmark)
endif()

if(WITH_FUZZ)
  add_executable(fuzz_categorizers test/fuzz_categorizers.cpp)
  target_link_libraries(fuzz_categorizers PRIVATE dwarfs_writer)
  list(APPEND BINARY_TARGETS fuzz_categorizers)

  add_executable(fuzz_mkdwarfs test/fuzz_mkdwarfs.cpp)
  target_link_libraries(fuzz_mkdwarfs PRIVATE mkdwarfs_main dwarfs_test_helpers)
  list(APPEND BINARY_TARGETS fuzz_mkdwarfs)
endif()

if(WITH_MAN_PAGES)
  list(REMOVE_DUPLICATES MAN_DIRS)
  add_custom_target(manpages ALL DEPENDS ${MAN_PAGES})
endif()

if(DWARFS_GIT_BUILD)
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
else()
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_library(
  dwarfs_thrift_lite
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
  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp2/frozen_data.cpp
  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp2/frozen_types.cpp
)

set_property(TARGET dwarfs_thrift_lite PROPERTY CXX_STANDARD 20)
target_link_libraries(dwarfs_thrift_lite PUBLIC dwarfs_folly_lite)

target_include_directories(dwarfs_thrift_lite SYSTEM PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
  ${THRIFT_GENERATED_DIR}
)

add_cpp2_thrift_library(fbthrift/thrift/lib/thrift/frozen.thrift
                        OUTPUT_PATH lib/thrift NO_LIBRARY)
add_cpp2_thrift_library(thrift/metadata.thrift FROZEN
                        TARGET dwarfs_metadata_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/compression.thrift
                        TARGET dwarfs_compression_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/history.thrift
                        TARGET dwarfs_history_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/features.thrift
                        TARGET dwarfs_features_thrift OUTPUT_PATH dwarfs)

foreach(tgt dwarfs_common dwarfs_reader dwarfs_writer dwarfs_extractor dwarfs_tool
            ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  set_target_properties(${tgt} PROPERTIES EXPORT_COMPILE_COMMANDS ON)

  target_link_libraries(${tgt} PUBLIC Boost::boost)
  target_link_libraries(${tgt} PRIVATE dwarfs_folly_lite dwarfs_thrift_lite range-v3::range-v3)

  if(USE_JEMALLOC)
    target_link_libraries(${tgt} PRIVATE PkgConfig::JEMALLOC)
  endif(USE_JEMALLOC)

  target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

  target_compile_definitions(
    ${tgt}
    PRIVATE DWARFS_HAVE_LIBZSTD
            DWARFS_STATIC_BUILD=${STATIC_BUILD_DO_NOT_USE}
            $<$<BOOL:${WITH_MAN_OPTION}>:DWARFS_BUILTIN_MANPAGE>
            $<$<BOOL:${USE_JEMALLOC}>:DWARFS_USE_JEMALLOC>
            $<$<BOOL:${LIBMAGIC_FOUND}>:DWARFS_HAVE_LIBMAGIC>
            $<$<BOOL:${LIBLZ4_FOUND}>:DWARFS_HAVE_LIBLZ4>
            $<$<BOOL:${LIBLZMA_FOUND}>:DWARFS_HAVE_LIBLZMA>
            $<$<AND:$<BOOL:${LIBBROTLIDEC_FOUND}>,$<BOOL:${LIBBROTLIENC_FOUND}>>:DWARFS_HAVE_LIBBROTLI>
            $<$<BOOL:${FLAC_FOUND}>:DWARFS_HAVE_FLAC>
            $<$<BOOL:${ENABLE_RICEPP}>:DWARFS_HAVE_RICEPP>
  )

  if(ENABLE_PERFMON)
    target_compile_definitions(${tgt} PRIVATE DWARFS_PERFMON_ENABLED=1)
  endif()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
     "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -pedantic)
  endif()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(${tgt} PRIVATE -Wno-stringop-overflow)
  endif()

  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS OFF)

  add_dependencies(${tgt} dwarfs_metadata_thrift dwarfs_history_thrift)

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
    target_compile_definitions(${tgt} PRIVATE DWARFS_COVERAGE_ENABLED=1)
  endif()

  target_link_libraries(${tgt} PRIVATE fmt::fmt)

  if(WIN32)
    target_link_libraries(${tgt} PRIVATE ntdll.lib dbghelp.lib)
  endif()
endforeach()

# not sure why exactly, copied from fsst/CMakeLists.txt
if(CMAKE_BUILD_TYPE STREQUAL Release)
  set_source_files_properties(fsst/fsst_avx512.cpp PROPERTIES COMPILE_FLAGS -O1)
endif()

add_library(
  dwarfs_fsst OBJECT
  fsst/libfsst.cpp fsst/fsst_avx512.cpp fsst/fsst_avx512_unroll1.inc
  fsst/fsst_avx512_unroll2.inc fsst/fsst_avx512_unroll3.inc
  fsst/fsst_avx512_unroll4.inc)

target_include_directories(dwarfs_common SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fsst)
set_property(TARGET dwarfs_fsst PROPERTY CXX_STANDARD 20)
set_property(TARGET dwarfs_fsst PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET dwarfs_fsst PROPERTY CXX_EXTENSIONS OFF)

target_link_libraries(
  dwarfs_common
  PUBLIC
  Boost::boost
  Boost::chrono
  Boost::iostreams
  dwarfs_compression_thrift
  dwarfs_metadata_thrift
  dwarfs_history_thrift
  dwarfs_features_thrift
  dwarfs_fsst
)

# if(LIBMAGIC_FOUND)
#   target_link_libraries(dwarfs PkgConfig::LIBMAGIC)
# endif()

target_link_libraries(dwarfs_common PRIVATE libzstd_static)
target_link_libraries(dwarfs_writer PRIVATE libzstd_static)

if(LIBLZ4_FOUND)
  target_link_libraries(dwarfs_common PRIVATE PkgConfig::LIBLZ4)
endif()

if(LIBLZMA_FOUND)
  target_link_libraries(dwarfs_common PRIVATE PkgConfig::LIBLZMA)
endif()

if(FLAC_FOUND)
  target_link_libraries(dwarfs_common PRIVATE PkgConfig::FLAC)
endif()

if(LIBBROTLIDEC_FOUND AND LIBBROTLIENC_FOUND)
  target_link_libraries(dwarfs_common PRIVATE PkgConfig::LIBBROTLIDEC PkgConfig::LIBBROTLIENC)
endif()

target_link_libraries(dwarfs_extractor PRIVATE PkgConfig::LIBARCHIVE)

foreach(tgt ${MAIN_TARGETS} ${BINARY_TARGETS} ${TEST_TARGETS})
  target_link_libraries(${tgt} PRIVATE dwarfs_tool)
endforeach()

target_link_libraries(mkdwarfs_main PRIVATE dwarfs_reader dwarfs_writer range-v3::range-v3)
target_link_libraries(dwarfsck_main PRIVATE dwarfs_reader)
target_link_libraries(dwarfsextract_main PRIVATE dwarfs_extractor)
target_link_libraries(dwarfsbench_main PRIVATE dwarfs_extractor)

target_link_libraries(mkdwarfs PRIVATE mkdwarfs_main)
target_link_libraries(dwarfsck PRIVATE dwarfsck_main)
target_link_libraries(dwarfsextract PRIVATE dwarfsextract_main)
target_link_libraries(dwarfsbench PRIVATE dwarfsbench_main)

if(TARGET dwarfsuniversal)
  target_link_libraries(dwarfsuniversal PRIVATE dwarfs_writer dwarfs_extractor)
endif()
if(TARGET dwarfs_main)
  target_link_libraries(dwarfs_main PRIVATE dwarfs_reader)
endif()
if(TARGET dwarfs2_main)
  target_link_libraries(dwarfs2_main PRIVATE dwarfs_reader)
endif()
if(TARGET dwarfs_benchmark)
  target_link_libraries(dwarfs_benchmark PRIVATE dwarfs_reader dwarfs_writer)
endif()
if(TARGET multiversioning_benchmark)
  target_link_libraries(multiversioning_benchmark PRIVATE dwarfs_writer)
endif()
if(TARGET segmenter_benchmark)
  target_link_libraries(segmenter_benchmark PRIVATE dwarfs_writer)
endif()

foreach(tgt ${TEST_TARGETS})
  target_link_libraries(${tgt} PRIVATE dwarfs_writer dwarfs_extractor)
endforeach()

if(STATIC_BUILD_DO_NOT_USE)
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
  if(PREFER_SYSTEM_LIBFMT)
    import_static_lib(static_libfmt "libfmt.a")
  endif()
  import_static_lib(static_libdoubleconv "libdouble-conversion.a")
  import_static_lib(static_libgflags "libgflags.a")
  import_static_lib(static_libevent "libevent.a")
  import_static_lib(static_libacl "libacl.a")
  import_static_lib(static_libxml2 "libxml2.a")
  import_static_lib(static_libcrypto "libcrypto.a")
  import_static_lib(static_libz "libz.a")
  import_static_lib(static_libpthread "libpthread.a")
  import_static_lib(static_libdl "libdl.a")
  import_static_lib(static_libm "libm.a")
  import_static_lib(static_librt "librt.a")
  import_static_lib(static_libssl "libssl.a")
  import_static_lib(static_libunwind "libunwind.a")
  import_static_lib(static_libarchive "libarchive.a")
  import_static_lib(static_libmagic "libmagic.a")
  import_static_lib(static_libflac "libFLAC++.a")

  set_target_properties(static_libunwind PROPERTIES INTERFACE_LINK_LIBRARIES
                                                    "${LIBLZMA_LIBRARIES};static_libz")
  set_target_properties(static_libglog PROPERTIES INTERFACE_LINK_LIBRARIES
                                                  static_libgflags)
  set_target_properties(static_librt PROPERTIES INTERFACE_LINK_LIBRARIES
                                                static_libgflags)
  set_target_properties(static_libmagic PROPERTIES INTERFACE_LINK_LIBRARIES
                                                static_libz)

  foreach(tgt ${BINARY_TARGETS} ${TEST_TARGETS})
    if(PREFER_SYSTEM_LIBFMT)
      target_link_libraries(
        ${tgt} PRIVATE
        static_libfmt)
    endif()

    # ...................................................................
    # -static-libgcc above and gcc_eh below is all together an ugly trick to
    # enforce static linking
    # ...................................................................
    target_link_libraries(
      ${tgt}
      PRIVATE
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
      static_libm
      static_librt
      gcc_eh
      static_libunwind)
  endforeach()
endif(STATIC_BUILD_DO_NOT_USE)

foreach(tgt ${TEST_TARGETS})
  list(APPEND REALCLEAN_FILES "${tgt}[1]_include.cmake")
  list(APPEND REALCLEAN_FILES $<TARGET_FILE:${tgt}>.manifest)
endforeach()

foreach(tgt ${BINARY_TARGETS})
  list(APPEND REALCLEAN_FILES $<TARGET_FILE:${tgt}>.manifest)
endforeach()

add_custom_target(
  realclean
  COMMAND
    COMMAND ${CMAKE_MAKE_PROGRAM} clean
  COMMAND
    ${CMAKE_COMMAND} -E rm -rf
        CMakeCache.txt CMakeFiles
        cmake_install.cmake install_manifest.txt
        dwarfs_install.cmake package_version.cmake
        CPackConfig.cmake CPackSourceConfig.cmake _CPack_Packages
        CTestTestfile.cmake Testing
        fbthrift folly zstd ricepp
        include src thrift universal bin lib man1 man5
        vcpkg-manifest-install.log
        build.ninja .ninja_deps
        Makefile compile_commands.json
        artifacts.env
        default.profraw
        profile
  COMMAND
    ${CMAKE_COMMAND} -E rm -rf ${REALCLEAN_FILES}
)

add_custom_target(
  distclean
  COMMAND
    COMMAND ${CMAKE_MAKE_PROGRAM} realclean
  COMMAND
    ${CMAKE_COMMAND} -E rm -rf _deps
)

file(GLOB_RECURSE ALL_SOURCES LIST_DIRECTORIES false
        ${CMAKE_CURRENT_SOURCE_DIR}/ricepp/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/ricepp/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp)

add_custom_target(
  format
  COMMAND clang-format -i ${ALL_SOURCES})

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

if(STATIC_BUILD_DO_NOT_USE OR APPLE)
  foreach(tgt ${BINARY_TARGETS})
    list(APPEND FILES_TO_STRIP $<TARGET_FILE:${tgt}>)
  endforeach()
  if(APPLE)
    add_custom_target(strip COMMAND strip ${FILES_TO_STRIP})
  else()
    add_custom_target(strip COMMAND strip -s ${FILES_TO_STRIP})
  endif()
endif()

if(PRJ_VERSION_FULL)
  set(DWARFS_ARTIFACT_ID "${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  if(USE_RICEPP_PACKAGE)
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-ricepp")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-gcc")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-clang")
  endif()
  if(DWARFS_OPTIMIZE)
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-O${DWARFS_OPTIMIZE}")
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-reldbg")
  elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-debug")
  endif()
endif()

if(STATIC_BUILD_DO_NOT_USE OR WIN32)
  if (WITH_UNIVERSAL_BINARY)
    find_program(UPX_EXE upx upx.exe PATHS "c:/bin" DOC "ultimate packer for executables" REQUIRED)

    set(UNIVERSAL_PACKED
            "dwarfs-universal-${DWARFS_ARTIFACT_ID}${CMAKE_EXECUTABLE_SUFFIX}")

    # upx -9 is a good compromise between compression ratio and speed
    # also, anything above --best increases the startup time of the compressed
    # executable significantly
    add_custom_command(
      OUTPUT ${UNIVERSAL_PACKED}
      COMMAND ${UPX_EXE} -9 -o ${UNIVERSAL_PACKED} $<TARGET_FILE:dwarfsuniversal>
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_custom_target(universal_upx DEPENDS ${UNIVERSAL_PACKED})
  endif()

  if(DEFINED ENV{GITHUB_REF_TYPE})
    message(STATUS "GITHUB_REF_TYPE: $ENV{GITHUB_REF_TYPE}")
    message(STATUS "GITHUB_REF_NAME: $ENV{GITHUB_REF_NAME}")
    message(STATUS "GITHUB_RUN_ID: $ENV{GITHUB_RUN_ID}")
    message(STATUS "GITHUB_RUN_NUMBER: $ENV{GITHUB_RUN_NUMBER}")
    message(STATUS "GITHUB_RUN_ATTEMPT: $ENV{GITHUB_RUN_ATTEMPT}")

    if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
      set(ARTIFACTS_SUBDIR "releases/$ENV{GITHUB_REF_NAME}@${PRJ_GIT_REV}")
    else()
      set(ARTIFACTS_SUBDIR "builds/$ENV{GITHUB_RUN_NUMBER}.$ENV{GITHUB_RUN_ATTEMPT}-${PRJ_VERSION_FULL}")
    endif()

    set(ARTIFACTS_FULL_PATH "${DWARFS_ARTIFACTS_DIR}/${ARTIFACTS_SUBDIR}")

    if(WIN32)
      set(PACKAGE_EXT ".7z")
    else()
      set(PACKAGE_EXT ".tar.zst")
    endif()

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/artifacts.env"
               "binary_tarball=${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}${PACKAGE_EXT}\n"
               "universal_binary=${UNIVERSAL_PACKED}\n")

    if(USE_RICEPP_PACKAGE)
      add_executable(ricepp_benchmark_fits ricepp/ricepp_benchmark_fits.cpp)
      target_link_libraries(ricepp_benchmark_fits PRIVATE ricepp benchmark::benchmark)
      set(RICEPP_BENCHMARK_FITS "ricepp_benchmark_fits")
    else()
      set(RICEPP_BENCHMARK_FITS "ricepp/ricepp_benchmark_fits")
    endif()

    add_custom_target(copy_artifacts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}${PACKAGE_EXT} ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${UNIVERSAL_PACKED} ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy
          ${CMAKE_CURRENT_BINARY_DIR}/${RICEPP_BENCHMARK_FITS}${CMAKE_EXECUTABLE_SUFFIX}
          ${ARTIFACTS_FULL_PATH}/ricepp_benchmark_fits-${DWARFS_ARTIFACT_ID}${CMAKE_EXECUTABLE_SUFFIX}
    )

    add_custom_target(copy_source_artifacts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}${PACKAGE_EXT} ${ARTIFACTS_FULL_PATH}
    )
  endif()
endif()

if(NOT "${PRJ_VERSION_FULL}" STREQUAL "")
  if(DWARFS_GIT_BUILD)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/dwarfs_install.cmake.in" dwarfs_install.cmake @ONLY)
    set(CPACK_INSTALL_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/dwarfs_install.cmake")
  endif()

  if(WIN32)
    # set(CPACK_GENERATOR "NSIS;ZIP;7Z")
    set(CPACK_GENERATOR "7Z")
  else()
    # use TZST and later re-pack as TXZ
    set(CPACK_GENERATOR "TZST")
  endif()
  set(CPACK_SOURCE_GENERATOR "${CPACK_GENERATOR}")
  set(CPACK_PACKAGE_VERSION_MAJOR "${PRJ_VERSION_MAJOR}")
  set(CPACK_PACKAGE_VERSION_MINOR "${PRJ_VERSION_MINOR}")
  set(CPACK_PACKAGE_VERSION_PATCH "${PRJ_VERSION_PATCH}")
  set(CPACK_SOURCE_PACKAGE_FILE_NAME
      "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}")
  set(CPACK_PACKAGE_FILE_NAME
      "${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "dwarfs - A high compression read-only file system")
  set(CPACK_PACKAGE_VENDOR "Marcus Holland-Moritz <github@mhxnet.de>")
  set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  list(APPEND CPACK_SOURCE_IGNORE_FILES
    "\\.git/"
    "${CMAKE_SOURCE_DIR}/build.*"
    "${CMAKE_SOURCE_DIR}/@"
    "/\\."
    ".*~$"
    "${CMAKE_SOURCE_DIR}/doc/.*\\.png$"
    "${CMAKE_SOURCE_DIR}/doc/.*\\.gif$"
  )
  set(CPACK_VERBATIM_VARIABLES YES)
  set(CPACK_STRIP_FILES YES)

  include(CPack)
endif()
