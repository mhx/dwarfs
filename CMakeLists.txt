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

cmake_minimum_required(VERSION 3.24.0)

# Enable CMAKE_MSVC_RUNTIME_LIBRARY
cmake_policy(SET CMP0091 NEW)

project(dwarfs)

include(ExternalProject)

include(CheckCXXSourceCompiles)

option(WITH_TESTS "build with tests" OFF)
option(WITH_BENCHMARKS "build with benchmarks" OFF)
option(ENABLE_PERFMON "enable performance monitor in all tools" ON)
option(PREFER_SYSTEM_LIBFMT "use system libfmt if available" OFF)
if(WIN32)
  set(PREFER_SYSTEM_ZSTD ON)
  set(PREFER_SYSTEM_XXHASH ON)
else()
  option(WITH_LEGACY_FUSE "build fuse2 driver even if we have fuse3" OFF)
  option(WITH_MAN_PAGES "build man pages using ronn" ON)
  option(ENABLE_ASAN "enable address sanitizer" OFF)
  option(ENABLE_TSAN "enable thread sanitizer" OFF)
  option(ENABLE_UBSAN "enable undefined behaviour sanitizer" OFF)
  option(ENABLE_COVERAGE "enable code coverage" OFF)
  option(USE_JEMALLOC "build with jemalloc" ON)
  option(PREFER_SYSTEM_ZSTD "use system zstd if available" OFF)
  option(PREFER_SYSTEM_XXHASH "use system xxHash if available" OFF)
  option(PREFER_SYSTEM_GTEST "use system gtest if available" OFF)
  option(DISABLE_CCACHE "disable ccache" OFF)
  option(STATIC_BUILD_DO_NOT_USE "try static build (experimental)" OFF)
endif()

if(DEFINED ENV{DWARFS_LOCAL_REPO_PATH})
  set(LIBFMT_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/fmt)
  set(GOOGLETEST_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/googletest)
else()
  set(LIBFMT_GIT_REPO https://github.com/fmtlib/fmt.git)
  set(GOOGLETEST_GIT_REPO https://github.com/google/googletest.git)
endif()

if(NOT DISABLE_CCACHE)
  find_program(CCACHE_EXE ccache)
  if(CCACHE_EXE)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXE})
    message(STATUS "Using ccache at ${CCACHE_EXE}")
  else()
    message(STATUS "Could not find ccache")
  endif()
endif()

if(DEFINED ENV{CCACHE_PREFIX})
  add_compile_options(-Wno-gnu-line-marker)
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
  set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -g")
  set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -g")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_compile_options(-fcolor-diagnostics)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0.0")
    # for some reason, clang-10 binaries crash upon throwing exceptions
    # [maxirmx] This is true for clang-12 Release build, but no crash for RelWithDebugInfo
  add_compile_options(-fno-omit-frame-pointer)
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/Zc:__cplusplus /utf-8 /wd4267 /wd4244 /wd5219)
  # Apply /MT or /MTd  (multithread, static version of the run-time library)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/version.cmake)

# -----------------------------------------------------------------------------------------
message(STATUS ">>>>> Applying tebako settings")

# THis is the default that may be overwritten
set(GNU_BASH "bash")

# -------------------------------------------------------------------------------------------------------------------------------
# Build scopes
#  ALL -- build library, all tools            [WITH_TEST or WITH_BENCHMARKS enforce this scope]
#  MKD -- build library and mkdwarfs utility  [This is the configuration required for tebako]
#  LIB -- build library only                  [This configuration is required for cross compilation of tebako packaged software]
#                                             [For example in order to build MacOS arm64 package on MacOS x86_64               ]
# -------------------------------------------------------------------------------------------------------------------------------

if(NOT TEBAKO_BUILD_SCOPE)
  set(TEBAKO_BUILD_SCOPE "ALL")
  message(STATUS "Tebako build scope was not specified and has been set to ALL")
endif()
if((WITH_TESTS OR WITH_BENCHMARKS) AND NOT "${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL" )
  set(TEBAKO_BUILD_SCOPE "ALL")
  message(STATUS "WITH_TESTS or WITH_BENCHMARKS option was specified. Tebako build scope has been forcefully set to ALL")
endif()
if(NOT "${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL" AND
   NOT "${TEBAKO_BUILD_SCOPE}" STREQUAL "MKD" AND
   NOT "${TEBAKO_BUILD_SCOPE}" STREQUAL "LIB")
   message(STATUS "Unknown build scope setting ${TEBAKO_BUILD_SCOPE}. Tebako build scope has been forcefully set to ALL")
   set(TEBAKO_BUILD_SCOPE "ALL")
endif()

if (NOT DEPS)
  set(DEPS "${CMAKE_CURRENT_SOURCE_DIR}/deps")
  message(STATUS "Using local setting for DEPS folder: ${DEPS}")
  set(EXTERNAL_DEPS OFF)
else()
  message(STATUS "Using upstream setting for DEPS folder: ${DEPS}")
  set(EXTERNAL_DEPS ON)
endif()

set(DEPS_INCLUDE_DIR ${DEPS}/include)
set(DEPS_LIB_DIR ${DEPS}/lib)
set(DEPS_BIN_DIR ${DEPS}/bin)
set(DEPS_SBIN_DIR ${DEPS}/sbin)

set(TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/tools")

include(${TOOLS}/cmake-scripts/def-external-project.cmake)

include_directories(${DEPS_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/folly)
link_directories(${DEPS_LIB_DIR})

set(IS_MSYS OFF)
if(MSVC)
  set(OSTYPE_TXT "Windows")
  message(STATUS "OSTYPE (forced by MSVC): '${OSTYPE_TXT}'")
  set(PREFER_SYSTEM_LIBFMT ON)
  # This is for glog 0.7.0 that is installed by vcpkg
  add_compile_definitions(GLOG_USE_GLOG_EXPORT)
elseif(MINGW)
  set(OSTYPE_TXT "msys")
  message(STATUS "OSTYPE  (forced by MINGW): '${OSTYPE_TXT}'")
  set(IS_MSYS ON)
  # This is for glog 0.6.0 that we build ourself
  add_definitions(-DGLOG_USE_GLOG_EXPORT)
  include(${TOOLS}/cmake-scripts/setup-libarchive.cmake)
  else(MSVC)
  include(${TOOLS}/cmake-scripts/setup-libarchive.cmake)

  execute_process(
    COMMAND "${GNU_BASH}"
            "-c"
            "echo \$OSTYPE"
    RESULT_VARIABLE OSTYPE_RES
    OUTPUT_VARIABLE OSTYPE_TXT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )

  if(OSTYPE_RES EQUAL 0)
    message(STATUS "OSTYPE: '${OSTYPE_TXT}'")
  else(OSTYPE_RES EQUAL 0)
    message(FATAL_ERROR "Failed to detect OSTYPE: ${OSTYPE_TXT}")
  endif(OSTYPE_RES EQUAL 0)
endif(MSVC)

set(PATCH_FOLLY OFF)
set(PATCH_FBTHRIFT OFF)

if("${OSTYPE_TXT}" MATCHES "^linux-musl.*")
  message(STATUS "Adding __musl__ compile definition")
  add_compile_definitions(__musl__)
  include(${TOOLS}/cmake-scripts/setup-libutfcpp.cmake)
  add_dependencies(_LIBARCHIVE ${LIBUTFCPP_PRJ})
  if (NOT NO_PATCH)
    set(PATCH_FOLLY ON)
  endif(NOT NO_PATCH)
endif()

if ("${OSTYPE_TXT}" MATCHES "^darwin*")
  include(${TOOLS}/cmake-scripts/macos-environment.cmake)
  include(${TOOLS}/cmake-scripts/setup-libutfcpp.cmake)
  add_dependencies(_LIBARCHIVE ${LIBUTFCPP_PRJ})
  if (NOT NO_PATCH)
    set(PATCH_FOLLY ON)
  endif(NOT NO_PATCH)
endif()

if ("${OSTYPE_TXT}" MATCHES "msys")
  include(${TOOLS}/cmake-scripts/msys-environment.cmake)
  include(${TOOLS}/cmake-scripts/setup-libutfcpp.cmake)
  add_dependencies(_LIBARCHIVE ${LIBUTFCPP_PRJ})
  find_library(WINSOCK ws2_32 REQUIRED)
  message(STATUS "Using winsock2 at ${WINSOCK}")

  if (NOT NO_PATCH)
    set(PATCH_FOLLY ON)
    set(PATCH_FBTHRIFT ON)
  endif(NOT NO_PATCH)
endif()

if (${PATCH_FOLLY})
  set(FOLLY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/folly")
  message(STATUS "Patching folly with '${GNU_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci-scripts/patch-folly.sh ${FOLLY_ROOT}'")
  execute_process(
    COMMAND "${GNU_BASH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci-scripts/patch-folly.sh"
            "${FOLLY_ROOT}"
    RESULT_VARIABLE PATCH_FOLLY_RES
    OUTPUT_VARIABLE PATCH_FOLLY_TXT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )

  if(PATCH_FOLLY_RES EQUAL 0)
    message(STATUS "Patched folly at '${FOLLY_ROOT}'")
  else(PATCH_FOLLY_RES EQUAL 0)
    message(FATAL_ERROR "Failed to patch folly at '${FOLLY_ROOT}': ${PATH_FOLLY_TXT}")
  endif(PATCH_FOLLY_RES EQUAL 0)
endif(${PATCH_FOLLY})

if (${PATCH_FBTHRIFT})
  set(FBTHRIFT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/fbthrift")
  message(STATUS "Patching fbthrift with '${GNU_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci-scripts/patch-fbthrift.sh ${FBTHRIFT_ROOT}'")
  execute_process(
    COMMAND "${GNU_BASH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci-scripts/patch-fbthrift.sh"
            "${FBTHRIFT_ROOT}"
    RESULT_VARIABLE PATCH_FBTHRIFT_RES
    OUTPUT_VARIABLE PATCH_FBTHRIFT_TXT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )

  if(PATCH_FBTHRIFT_RES EQUAL 0)
    message(STATUS "Patched fbthrift at '${FBTHRIFT_ROOT}'")
  else(PATCH_FBTHRIFT_RES EQUAL 0)
    message(FATAL_ERROR "Failed to patch fbthrift at '${FBTHRIFT_ROOT}': ${PATCH_FBTHRIFT_TXT}")
  endif(PATCH_FBTHRIFT_RES EQUAL 0)
endif(${PATCH_FBTHRIFT})

check_cxx_source_compiles(
  "#include <filesystem>
  int main() {
    std::filesystem::path p = \"\";
    std::u8string s;
    p = p / s;
    return 0;
  }"
  U8STRING_AND_PATH_OK)

if(U8STRING_AND_PATH_OK)
  add_compile_definitions(U8STRING_AND_PATH_OK)
endif()

message(STATUS "<<<<< Applied tebako settings")
# -----------------------------------------------------------------------------------------

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules")

if(STATIC_BUILD_DO_NOT_USE)
  if(STATIC_BUILD_EXTRA_PREFIX)
    include_directories(BEFORE ${STATIC_BUILD_EXTRA_PREFIX}/include)
    set(CMAKE_PREFIX_PATH ${STATIC_BUILD_EXTRA_PREFIX})
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

if(PREFER_SYSTEM_LIBFMT)
  find_package(fmt 10 CONFIG)
else(PREFER_SYSTEM_LIBFMT)

  include(FetchContent)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY ${LIBFMT_GIT_REPO}
    GIT_TAG 10.2.1
    OVERRIDE_FIND_PACKAGE
  )
  FetchContent_MakeAvailable(fmt)

  target_include_directories(fmt BEFORE PUBLIC
    $<BUILD_INTERFACE:${fmt_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )

  include_directories(BEFORE ${fmt_SOURCE_DIR}/include)
endif(PREFER_SYSTEM_LIBFMT)

find_package(Boost 1.67 REQUIRED COMPONENTS chrono iostreams program_options)

if(STATIC_BUILD_DO_NOT_USE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_package(PkgConfig)

if(STATIC_BUILD_DO_NOT_USE)
  if (PKG_CONFIG_FOUND)
    list(APPEND PKG_CONFIG_EXECUTABLE "--static")
  endif()
endif()

if(PKG_CONFIG_FOUND)
  pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.9.9)
  pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3>=3.9.0)
  pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=1.9.2)
  pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=5.2.4)
  if(NOT IS_MSYS)
    pkg_check_modules(LIBBROTLIDEC IMPORTED_TARGET libbrotlidec>=1.0.7)
    pkg_check_modules(LIBBROTLIENC IMPORTED_TARGET libbrotlienc>=1.0.7)
    pkg_check_modules(LIBBROTLICOMMON IMPORTED_TARGET libbrotlicommon>=1.0.7)
  endif(NOT IS_MSYS)
  if (MSVC)
    pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive>=3.6.0)
  endif(MSVC)
  pkg_check_modules(ZSTD IMPORTED_TARGET libzstd>=1.5.2)
  pkg_check_modules(XXHASH IMPORTED_TARGET libxxhash>=0.8.1)
endif()

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

if ("${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL")
  if(WIN32)
    if(NOT WINFSP_PATH)
      set(WINFSP_PATH "C:/Program Files (x86)/WinFsp")
    endif()
    find_library(WINFSP winfsp-x64.lib "${WINFSP_PATH}/lib")
    if (NOT WINFSP)
      message(WARNING "No WinFsp library found")
      add_compile_definitions(WITHOUT_FUSE)
    endif()
  else()
    if(NOT FUSE_FOUND AND NOT FUSE3_FOUND)
      message(WARNING "No FUSE or FUSE3 library found")
      add_compile_definitions(WITHOUT_FUSE)
    endif()
  endif()
endif("${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL")

if(USE_JEMALLOC)
  pkg_check_modules(JEMALLOC IMPORTED_TARGET jemalloc>=5.2.1)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  foreach(CompilerFlag CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
    string(REPLACE "/RTC1" "" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()

  set(MSVC_USE_STATIC_RUNTIME ON CACHE BOOL "static build")
endif()

set(ZSTD_INCLUDE_DIR
    ""
    CACHE PATH "don't build folly with zstd" FORCE)

set(THRIFT_COMPILER_ONLY
    ON
    CACHE BOOL "only build thrift compiler")

# TODO: this is due to a bug in folly's Portability.h
add_compile_definitions(FOLLY_CFG_NO_COROUTINES)

add_compile_definitions(GLOG_NO_ABBREVIATED_SEVERITIES NOMINMAX NOGDI)

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

add_subdirectory(folly EXCLUDE_FROM_ALL)                        # tebako -- was ... EXCLUDE_FROM_ALL SYSTEM ...  but it requires CMAKE 3.25
                                                                # which requires installation on Alpine 3.18 which needs gcompat
                                                                # that I would like to avoid
add_subdirectory(fbthrift EXCLUDE_FROM_ALL)                     # tebako -- was ... EXCLUDE_FROM_ALL SYSTEM ...  but it requires CMAKE 3.25
if(NOT (ZSTD_FOUND AND PREFER_SYSTEM_ZSTD))
  set(ZSTD_BUILD_SHARED OFF)
  add_subdirectory(zstd/build/cmake EXCLUDE_FROM_ALL)           # tebako -- was ... EXCLUDE_FROM_ALL SYSTEM ...  but it requires CMAKE 3.25
endif()

set_property(TARGET folly PROPERTY CXX_STANDARD 20)
set_property(TARGET folly_base PROPERTY CXX_STANDARD 20)

set(ZSTD_LIBRARY_RELEASE
    "ZSTD_LIBRARY_RELEASE-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)
set(ZSTD_LIBRARY_DEBUG
    "ZSTD_LIBRARY_DEBUG-NOTFOUND"
    CACHE FILEPATH "don't build folly with zstd" FORCE)

if(WITH_TESTS)
  if(NOT PREFER_SYSTEM_GTEST)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY ${GOOGLETEST_GIT_REPO}
      GIT_TAG v1.13.0
      OVERRIDE_FIND_PACKAGE
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  include(GoogleTest)
endif()

if(NOT
   (XXHASH_FOUND
    AND XXHASH_VERSION_OK
    AND PREFER_SYSTEM_XXHASH))
  add_library(xxhash xxHash/xxhash.c)

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
     "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
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
endif()

list(
  APPEND
  LIBDWARFS_SRC
  src/dwarfs/block_cache.cpp
  src/dwarfs/block_compressor.cpp
  src/dwarfs/block_manager.cpp
  src/dwarfs/block_range.cpp
  src/dwarfs/builtin_script.cpp
  src/dwarfs/cached_block.cpp
  src/dwarfs/checksum.cpp
  src/dwarfs/chmod_transformer.cpp
  src/dwarfs/console_writer.cpp
  src/dwarfs/entry.cpp
  src/dwarfs/error.cpp
  src/dwarfs/file_scanner.cpp
  src/dwarfs/file_stat.cpp
  src/dwarfs/file_type.cpp
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
  src/dwarfs/option_map.cpp
  src/dwarfs/options.cpp
  src/dwarfs/os_access_generic.cpp
  src/dwarfs/performance_monitor.cpp
  src/dwarfs/progress.cpp
  src/dwarfs/safe_main.cpp
  src/dwarfs/scanner.cpp
  src/dwarfs/similarity.cpp
  src/dwarfs/string_table.cpp
  src/dwarfs/terminal.cpp
  src/dwarfs/util.cpp
  src/dwarfs/wcwidth.c
  src/dwarfs/worker_group.cpp)

if(DWARFS_GIT_BUILD)
  list(APPEND LIBDWARFS_SRC ${CMAKE_CURRENT_BINARY_DIR}/src/dwarfs/version.cpp)
  include_directories(dwarfs PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
else()
  list(APPEND LIBDWARFS_SRC src/dwarfs/version.cpp)
endif()

list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/null.cpp)
list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/zstd.cpp)

if(LIBLZMA_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/lzma.cpp)
endif()

if(LIBLZ4_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/lz4.cpp)
endif()

if(LIBBROTLIDEC_FOUND AND LIBBROTLIENC_FOUND AND LIBBROTLICOMMON_FOUND)
  list(APPEND LIBDWARFS_COMPRESSION_SRC src/dwarfs/compression/brotli.cpp)
endif()

add_library(dwarfs ${LIBDWARFS_SRC})
add_library(dwarfs_compression ${LIBDWARFS_COMPRESSION_SRC})
add_library(dwarfs_tool src/dwarfs/tool.cpp)

# [maxirmx] For me it does not make sense but is required on Ubuntu 20.04 (gcc 10.5)
target_link_libraries(dwarfs_tool dwarfs)

target_compile_definitions(
  dwarfs_tool PRIVATE PRJ_BUILD_ID="${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SYSTEM}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

if(STATIC_BUILD_DO_NOT_USE)
  add_link_options(-static -static-libgcc)
endif(STATIC_BUILD_DO_NOT_USE)

if("${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL" OR
   "${TEBAKO_BUILD_SCOPE}" STREQUAL "MKD")
   add_library(mkdwarfs_main src/mkdwarfs_main.cpp)

   add_executable(mkdwarfs src/mkdwarfs.cpp)
   target_link_libraries(mkdwarfs mkdwarfs_main)

   list(APPEND BINARY_TARGETS mkdwarfs)
   list(APPEND MAIN_TARGETS mkdwarfs_main)

   install(TARGETS mkdwarfs RUNTIME DESTINATION bin)
endif()

if("${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL")
  add_library(dwarfsck_main src/dwarfsck_main.cpp)
  add_library(dwarfsbench_main src/dwarfsbench_main.cpp)
  add_library(dwarfsextract_main src/dwarfsextract_main.cpp)

  add_executable(dwarfsck src/dwarfsck.cpp)
  add_executable(dwarfsbench src/dwarfsbench.cpp)
  add_executable(dwarfsextract src/dwarfsextract.cpp)
  add_executable(dwarfsuniversal src/universal.cpp)

  target_link_libraries(dwarfsck dwarfsck_main)
  target_link_libraries(dwarfsbench dwarfsbench_main)
  target_link_libraries(dwarfsextract dwarfsextract_main)
  target_link_libraries(dwarfsuniversal mkdwarfs_main dwarfsck_main
        dwarfsextract_main)
  set_target_properties(dwarfsuniversal PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY universal
        OUTPUT_NAME dwarfs-universal)

  install(TARGETS dwarfsck dwarfsbench dwarfsextract RUNTIME DESTINATION bin)

  list(APPEND BINARY_TARGETS dwarfsck dwarfsbench dwarfsextract)
  list(APPEND BINARY_TARGETS dwarfsuniversal)
  list(APPEND MAIN_TARGETS dwarfsck_main dwarfsbench_main
        dwarfsextract_main)
  if(FUSE3_FOUND OR WINFSP)
    add_library(dwarfs_main src/dwarfs_main.cpp)
    target_compile_definitions(dwarfs_main PRIVATE _FILE_OFFSET_BITS=64)
    if(WINFSP)
      target_compile_definitions(dwarfs_main PRIVATE FUSE_USE_VERSION=32
                                                   DWARFS_FUSE_LOWLEVEL=0)
      target_include_directories(dwarfs_main PRIVATE "${WINFSP_PATH}/inc")
      target_link_libraries(dwarfs_main ${WINFSP})
      target_link_libraries(dwarfsuniversal delayimp.lib)
      target_link_options(dwarfsuniversal PRIVATE /DELAYLOAD:winfsp-x64.dll)
    else(WINFSP)
      target_compile_definitions(dwarfs_main PRIVATE FUSE_USE_VERSION=35)
      target_link_libraries(dwarfs_main PkgConfig::FUSE3)
    endif(WINFSP)
    add_executable(dwarfs-bin src/dwarfs.cpp)
    target_link_libraries(dwarfs-bin dwarfs_main)
    set_target_properties(dwarfs-bin PROPERTIES OUTPUT_NAME dwarfs)
    target_link_libraries(dwarfsuniversal dwarfs_main)
    if(WINFSP)
      install(TARGETS dwarfs-bin RUNTIME DESTINATION bin)
    else(WINFSP)
      add_custom_command(OUTPUT  mount.dwarfs
                       COMMAND ${CMAKE_COMMAND} -E create_symlink dwarfs mount.dwarfs
                       DEPENDS dwarfs-bin)
      list(APPEND SYMLINKS mount.dwarfs)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs DESTINATION sbin)
      install(TARGETS dwarfs-bin RUNTIME DESTINATION sbin)
    endif(WINFSP)
    list(APPEND BINARY_TARGETS dwarfs-bin)
    list(APPEND MAIN_TARGETS dwarfs_main)
  endif(FUSE3_FOUND OR WINFSP)

if(FUSE_FOUND AND (WITH_LEGACY_FUSE OR NOT FUSE3_FOUND))
  add_library(dwarfs2_main src/dwarfs_main.cpp)
  target_compile_definitions(dwarfs2_main PRIVATE _FILE_OFFSET_BITS=64
                                                  FUSE_USE_VERSION=29)
  target_link_libraries(dwarfs2_main PkgConfig::FUSE)
  add_executable(dwarfs2-bin src/dwarfs.cpp)
  target_link_libraries(dwarfs2-bin dwarfs2_main)
    if(NOT FUSE3_FOUND)
      target_link_libraries(dwarfsuniversal dwarfs2_main)
    endif(NOT FUSE3_FOUND)
    set_target_properties(dwarfs2-bin PROPERTIES OUTPUT_NAME dwarfs2)
    add_custom_command(OUTPUT  mount.dwarfs2
                       COMMAND ${CMAKE_COMMAND} -E create_symlink dwarfs2 mount.dwarfs2
                       DEPENDS dwarfs2-bin)
    list(APPEND SYMLINKS mount.dwarfs2)
    install(TARGETS dwarfs2-bin RUNTIME DESTINATION sbin)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mount.dwarfs2 DESTINATION sbin)
    list(APPEND BINARY_TARGETS dwarfs2-bin)
    list(APPEND MAIN_TARGETS dwarfs2_main)
  endif(FUSE_FOUND AND (WITH_LEGACY_FUSE OR NOT FUSE3_FOUND))
endif("${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL")

add_custom_target(symlinks ALL DEPENDS ${SYMLINKS})

if(WITH_TESTS OR WITH_BENCHMARKS)
  add_library(test_helpers test/test_helpers.cpp test/test_strings.cpp
                           test/loremipsum.cpp test/test_dirtree.cpp
                           test/filter_test_data.cpp)
  target_link_libraries(test_helpers dwarfs folly)
  set_property(TARGET test_helpers PROPERTY CXX_STANDARD 20)
endif()

if(WITH_TESTS)
  add_executable(dwarfs_test test/dwarfs.cpp)
  add_executable(dwarfs_compat_test test/dwarfs_compat.cpp)
  add_executable(dwarfs_badfs_test test/dwarfs_badfs.cpp)
  if(FUSE3_FOUND OR WINFSP OR FUSE_FOUND)
    add_executable(dwarfs_tools_test test/dwarfs_tools.cpp)
  endif()
  add_executable(dwarfs_utils_test test/utils_test.cpp)

  target_link_libraries(dwarfs_test test_helpers gtest gtest_main)
  target_link_libraries(dwarfs_compat_test gtest gtest_main)
  target_link_libraries(dwarfs_badfs_test gtest gtest_main)
  if(FUSE3_FOUND OR WINFSP OR FUSE_FOUND)
    target_link_libraries(dwarfs_tools_test test_helpers gtest gtest_main)
  endif()
  target_link_libraries(dwarfs_utils_test gtest gtest_main)

  list(APPEND BINARY_TARGETS dwarfs_test dwarfs_compat_test dwarfs_badfs_test
       dwarfs_utils_test)

  if(FUSE3_FOUND OR WINFSP OR FUSE_FOUND)
    list(APPEND BINARY_TARGETS dwarfs_tools_test)
  endif()

  gtest_discover_tests(dwarfs_test DISCOVERY_TIMEOUT 120)
  gtest_discover_tests(dwarfs_compat_test DISCOVERY_TIMEOUT 120)
  gtest_discover_tests(dwarfs_badfs_test DISCOVERY_TIMEOUT 120)
  if(FUSE3_FOUND OR WINFSP OR FUSE_FOUND)
    gtest_discover_tests(dwarfs_tools_test DISCOVERY_TIMEOUT 120)
  endif()
  gtest_discover_tests(dwarfs_utils_test DISCOVERY_TIMEOUT 120)

  target_compile_definitions(dwarfs_compat_test
                             PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\")

  target_compile_definitions(dwarfs_badfs_test
                             PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\")

  if(FUSE3_FOUND OR WINFSP OR FUSE_FOUND)
    target_compile_definitions(
      dwarfs_tools_test
      PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\"
              TOOLS_BIN_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\")
  endif()
endif()

if(WITH_BENCHMARKS)
  pkg_check_modules(BENCHMARK IMPORTED_TARGET benchmark)
  add_executable(dwarfs_benchmark test/dwarfs_benchmark.cpp)
  target_link_libraries(dwarfs_benchmark test_helpers PkgConfig::BENCHMARK)
  list(APPEND BINARY_TARGETS dwarfs_benchmark)
endif()

if(WITH_MAN_PAGES)
  list(REMOVE_DUPLICATES MAN_DIRS)
  add_custom_target(manpages ALL DEPENDS ${MAN_PAGES})
endif()

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
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_clients.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_constants.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_constants.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_for_each_field.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_handlers.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_metadata.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_metadata.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.tcc
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types_custom_protocol.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visit_by_thrift_field_metadata.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visit_union.h
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_visitation.h)

add_custom_command(
  OUTPUT thrift/lib/thrift/_keep
  COMMAND ${CMAKE_COMMAND} -E make_directory thrift/lib/thrift
  COMMAND ${CMAKE_COMMAND} -E touch thrift/lib/thrift/_keep
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(
  OUTPUT ${FROZEN_THRIFT_SRC}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/thrift/frozen.thrift
              ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
              -I ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
              -o ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift
              --gen mstch_cpp2
              frozen.thrift
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
          ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift/_keep
          ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/thrift/frozen.thrift
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/thrift/lib/thrift
)

add_custom_command(
  OUTPUT thrift/dwarfs/_keep
  COMMAND ${CMAKE_COMMAND} -E make_directory thrift/dwarfs
  COMMAND ${CMAKE_COMMAND} -E touch thrift/dwarfs/_keep
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(
  OUTPUT ${METADATA_THRIFT_SRC}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/thrift/metadata.thrift
              ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/metadata.thrift
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
              -I ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
              -o ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs
              --gen mstch_cpp2:frozen2
              metadata.thrift
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
          ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/_keep
          ${CMAKE_CURRENT_SOURCE_DIR}/thrift/metadata.thrift
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs
)

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

set(DWARFS_USE_EXCEPTION_TRACER OFF)
if(NOT STATIC_BUILD_DO_NOT_USE AND NOT FOLLY_NO_EXCEPTION_TRACER)
  if(TARGET folly_exception_tracer)
    message(STATUS "Folly exception tracer enabled")
    set(DWARFS_USE_EXCEPTION_TRACER ON)
  endif()
endif()

add_library(
  thrift_light
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/FieldRef.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift/thrift/lib/cpp2/protocol/Protocol.cpp
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

set_property(TARGET thrift_light PROPERTY CXX_STANDARD 20)
target_link_libraries(thrift_light folly)

target_include_directories(thrift_light PRIVATE ${INCLUDE_DIRS})

add_library(
  metadata_thrift
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_layouts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_types.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/thrift/dwarfs/gen-cpp2/metadata_data.cpp)

set_property(TARGET metadata_thrift PROPERTY CXX_STANDARD 20)

target_include_directories(metadata_thrift PRIVATE ${INCLUDE_DIRS})

target_link_libraries(metadata_thrift thrift_light)

foreach(tgt dwarfs dwarfs_compression dwarfs_tool ${BINARY_TARGETS} ${MAIN_TARGETS})
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
            $<$<AND:$<BOOL:${LIBBROTLIDEC_FOUND}>,$<BOOL:${LIBBROTLIENC_FOUND}>>:DWARFS_HAVE_LIBBROTLI>
  )

  if(DWARFS_USE_EXCEPTION_TRACER)
    target_compile_definitions(${tgt} PRIVATE DWARFS_USE_EXCEPTION_TRACER)
  endif()

  if(ENABLE_PERFMON)
    target_compile_definitions(${tgt} PRIVATE DWARFS_PERFMON_ENABLED=1)
  endif()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
     "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -pedantic)
  endif()

  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS OFF)

  add_dependencies(${tgt} metadata_thrift)
  if (NOT ("${OSTYPE_TXT}" MATCHES "Windows"))
    add_dependencies(${tgt} ${LIBARCHIVE_PRJ})
  endif()

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

list(APPEND LIBRARIES
  dwarfs
  dwarfs_tool
  metadata_thrift
  thrift_light
  folly
  ${WINSOCK}
  fsst
  ${Boost_LIBRARIES}
)

if (IS_MSYS)
  list(APPEND LIBRARIES glog gflags)
endif()

if(NOT PREFER_SYSTEM_LIBFMT)
  list(APPEND LIBRARIES fmt)
endif()

if(LIBLZ4_FOUND)
  list(APPEND LIBRARIES PkgConfig::LIBLZ4)
endif()

if(LIBLZMA_FOUND)
  list(APPEND LIBRARIES PkgConfig::LIBLZMA)
endif()

if(LIBBROTLIDEC_FOUND AND LIBBROTLIENC_FOUND AND LIBBROTLICOMMON_FOUND)
  list(APPEND LIBRARIES PkgConfig::LIBBROTLIDEC PkgConfig::LIBBROTLIENC PkgConfig::LIBBROTLICOMMON)
endif()

if(NOT STATIC_BUILD_DO_NOT_USE)
  if(MSVC)
    list(APPEND LIBRARIES PkgConfig::LIBARCHIVE)
  else(MSVC)
    list(APPEND LIBRARIES _LIBARCHIVE)
  endif(MSVC)
endif(NOT STATIC_BUILD_DO_NOT_USE)

if(ZSTD_FOUND AND PREFER_SYSTEM_ZSTD)
  list(APPEND LIBRARIES PkgConfig::ZSTD)
else()
  list(APPEND LIBRARIES libzstd_static)
endif()

if(XXHASH_FOUND
   AND XXHASH_VERSION_OK
   AND PREFER_SYSTEM_XXHASH)
   list(APPEND LIBRARIES PkgConfig::XXHASH)
else()
  list(APPEND LIBRARIES xxhash)
endif()

foreach(tgt ${BINARY_TARGETS} ${MAIN_TARGETS})
  target_link_libraries(${tgt} "$<LINK_LIBRARY:WHOLE_ARCHIVE,dwarfs_compression>")
  if(MSVC)
    target_link_libraries(${tgt} ${LIBRARIES} PkgConfig::LIBARCHIVE)
  else(MSVC)
    target_link_libraries(${tgt} ${LIBRARIES} _LIBARCHIVE)
  endif(MSVC)
  if(USE_JEMALLOC)
    target_link_libraries(${tgt} PkgConfig::JEMALLOC)
  endif(USE_JEMALLOC)
  if(DWARFS_USE_EXCEPTION_TRACER)
    target_link_libraries(${tgt} "$<LINK_LIBRARY:WHOLE_ARCHIVE,folly_exception_tracer_base>")
  endif()
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

  set_target_properties(static_libunwind PROPERTIES INTERFACE_LINK_LIBRARIES
                                                    PkgConfig::LIBLZMA)
  set_target_properties(static_libglog PROPERTIES INTERFACE_LINK_LIBRARIES
                                                  static_libgflags)
  set_target_properties(static_librt PROPERTIES INTERFACE_LINK_LIBRARIES
                                                static_libgflags)


  foreach(tgt ${BINARY_TARGETS})
    if(PREFER_SYSTEM_LIBFMT)
      target_link_libraries(
        ${tgt}
        static_libfmt)
    endif()

    # ...................................................................
    # -static-libgcc above and gcc_eh below is all together an ugly trick to
    # enforce static linking
    # ...................................................................
    target_link_libraries(
      ${tgt}
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
        "dwarfs_badfs_test[1]_include.cmake"
        "dwarfs_compat_test[1]_include.cmake"
        "dwarfs_test[1]_include.cmake"
        "dwarfs_tools_test[1]_include.cmake"
        "dwarfs_utils_test[1]_include.cmake"
        fbthrift folly zstd
        include src thrift universal bin lib man1 man5
        vcpkg-manifest-install.log
        dwarfs.exe.manifest dwarfsbench.exe.manifest
        dwarfsck.exe.manifest dwarfsextract.exe.manifest
        dwarfs_badfs_test.exe.manifest dwarfs_compat_test.exe.manifest
        dwarfs_test.exe.manifest dwarfs_tools_test.exe.manifest
        dwarfs_utils_test.exe.manifest mkdwarfs.exe.manifest
        install_manifest_dev.txt install_manifest_gmock.txt
        install_manifest_gtest.txt install_manifest_Unspecified.txt
        build.ninja .ninja_deps
        Makefile
)

add_custom_target(
  distclean
  COMMAND
    COMMAND ${CMAKE_MAKE_PROGRAM} realclean
  COMMAND
    ${CMAKE_COMMAND} -E rm -rf _deps
)

file(GLOB_RECURSE ALL_SOURCES LIST_DIRECTORIES false
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

if(STATIC_BUILD_DO_NOT_USE AND NOT WIN32)
  foreach(tgt ${BINARY_TARGETS})
    list(APPEND FILES_TO_STRIP $<TARGET_FILE:${tgt}>)
  endforeach()
  add_custom_target(strip COMMAND strip -s ${FILES_TO_STRIP})
endif()

if(STATIC_BUILD_DO_NOT_USE OR (WIN32 AND NOT IS_MSYS AND "${TEBAKO_BUILD_SCOPE}" STREQUAL "ALL"))
  find_program(UPX_EXE upx upx.exe PATHS "c:/bin" DOC "ultimate packer for executables" REQUIRED)

  set(UNIVERSAL_PACKED
          "dwarfs-universal-${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}${CMAKE_EXECUTABLE_SUFFIX}")

  add_custom_command(
    OUTPUT ${UNIVERSAL_PACKED}
    COMMAND ${UPX_EXE} -o ${UNIVERSAL_PACKED} $<TARGET_FILE:dwarfsuniversal>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  add_custom_target(universal_upx DEPENDS ${UNIVERSAL_PACKED})

  if(DEFINED ENV{GITHUB_REF_TYPE})
    if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
      set(ARTIFACTS_SUBDIR "releases/$ENV{GITHUB_REF_NAME}@${PRJ_GIT_REV}")
    else()
      set(ARTIFACTS_SUBDIR "builds/${PRJ_VERSION_FULL}")
    endif()

    set(ARTIFACTS_FULL_PATH "${DWARFS_ARTIFACTS_DIR}/${ARTIFACTS_SUBDIR}")

    if(WIN32)
      set(PACKAGE_EXT ".7z")
    else()
      set(PACKAGE_EXT ".tar.zst")
    endif()

    add_custom_target(copy_artifacts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}${PACKAGE_EXT} ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${UNIVERSAL_PACKED} ${ARTIFACTS_FULL_PATH}
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
      "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
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
