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
option(TRY_ENABLE_FLAC "build with FLAC support" ON)
option(ENABLE_RICEPP "build with RICEPP compression support" ON)
option(WITH_UNIVERSAL_BINARY "build with universal binary" ON)
option(WITH_PXATTR "build with pxattr binary" OFF)
if(NOT (APPLE OR WIN32))
  option(ENABLE_STACKTRACE "build with symbolizer support" ON)
endif()
if(APPLE)
  option(USE_HOMEBREW_LIBARCHIVE "use libarchive from homebrew" ON)
endif()
if(NOT WIN32)
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
  option(STATIC_BUILD_DO_NOT_USE "try static build (experimental)" OFF)
endif()

# Libraries that we can fetch on demand if necessary
#
# All of these libraries are header-only and not strictly required once
# libdwarfs is built. Only the range-v3 library is used to implement
# string splitting, but there's plenty of alternatives for that.
#
# We only use these libraries for building the binary targets, we won't
# install them.

set(LIBFMT_REQUIRED_VERSION 10.0)
set(LIBFMT_PREFERRED_VERSION 10.2.1)

set(GOOGLETEST_REQUIRED_VERSION 1.13.0)
set(GOOGLETEST_PREFERRED_VERSION 1.14.0)

set(RANGE_V3_REQUIRED_VERSION 0.12.0)
set(RANGE_V3_PREFERRED_VERSION 0.12.0)

set(PARALLEL_HASHMAP_REQUIRED_VERSION 1.3.8)
set(PARALLEL_HASHMAP_PREFERRED_VERSION 1.3.12)

set(BOOST_REQUIRED_VERSION 1.67.0)
set(LIBCRYPTO_REQUIRED_VERSION 3.0.0)
set(LIBLZ4_REQUIRED_VERSION 1.9.3)
set(LIBLZMA_REQUIRED_VERSION 5.2.5)
set(LIBBROTLI_REQUIRED_VERSION 1.0.9)
set(LIBARCHIVE_REQUIRED_VERSION 3.6.0)
set(LIBMAGIC_REQUIRED_VERSION 5.38)
set(ZSTD_REQUIRED_VERSION 1.4.8)
set(XXHASH_REQUIRED_VERSION 0.8.1)
set(FLAC_REQUIRED_VERSION 1.4.2)
set(JEMALLOC_REQUIRED_VERSION 5.2.1)

if(DEFINED ENV{DWARFS_LOCAL_REPO_PATH})
  set(LIBFMT_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/fmt)
  set(GOOGLETEST_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/googletest)
  set(RANGE_V3_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/range-v3)
  set(PARALLEL_HASHMAP_GIT_REPO $ENV{DWARFS_LOCAL_REPO_PATH}/parallel-hashmap)
else()
  set(LIBFMT_GIT_REPO https://github.com/fmtlib/fmt.git)
  set(GOOGLETEST_GIT_REPO https://github.com/google/googletest.git)
  set(RANGE_V3_GIT_REPO https://github.com/ericniebler/range-v3.git)
  set(PARALLEL_HASHMAP_GIT_REPO https://github.com/greg7mdp/parallel-hashmap.git)
endif()

if(APPLE)
  set(HOMEBREW_PREFIX_PATH "/opt/homebrew")
  set(TRY_RUN_INCLUDE_DIRECTORIES ${HOMEBREW_PREFIX_PATH}/include)
endif()

if(BUILD_SHARED_LIBS AND STATIC_BUILD_DO_NOT_USE)
  message(FATAL_ERROR "Seriously, don't try setting both BUILD_SHARED_LIBS and STATIC_BUILD_DO_NOT_USE")
endif()

if(WIN32 OR STATIC_BUILD_DO_NOT_USE)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
else()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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

if(ENABLE_STACKTRACE)
  if(CMAKE_BUILD_TYPE STREQUAL Release)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
       "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
      # enable line numbers in stack traces
      add_compile_options(-g1)
    endif()
  endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/version.cmake)

if(NOT PRJ_VERSION_FULL)
  message(FATAL_ERROR "PRJ_VERSION_FULL is not set")
endif()

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
endif()

if(WITH_MAN_PAGES)
  include("${CMAKE_SOURCE_DIR}/cmake/manpage.cmake")
  foreach(man dwarfs.1 mkdwarfs.1 dwarfsck.1 dwarfsextract.1 dwarfs-format.5)
    add_manpage(${man})
  endforeach()
endif()

find_package(fmt ${LIBFMT_REQUIRED_VERSION} CONFIG)

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

# folly won't build on Windows with FMT_HEADER_ONLY
if(fmt_FOUND OR STATIC_BUILD_DO_NOT_USE OR WIN32)
  set(DWARFS_FMT_LIB fmt::fmt)
else()
  add_compile_definitions(FMT_HEADER_ONLY)
  include_directories($<BUILD_INTERFACE:$<TARGET_PROPERTY:fmt::fmt-header-only,INTERFACE_INCLUDE_DIRECTORIES>>)
endif()

find_package(range-v3 ${RANGE_V3_REQUIRED_VERSION} CONFIG)

if(NOT range-v3_FOUND)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY ${RANGE_V3_GIT_REPO}
    GIT_TAG ${RANGE_V3_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(range-v3)
endif()

try_run(
  PHMAP_RUN_RESULT
  PHMAP_COMPILE_RESULT
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/checks/phmap-version.cpp
  CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${TRY_RUN_INCLUDE_DIRECTORIES}
  CXX_STANDARD 20
  RUN_OUTPUT_VARIABLE PHMAP_VERSION
  COMPILE_OUTPUT_VARIABLE PHMAP_COMPILE_OUTPUT
)

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

find_package(Boost ${BOOST_REQUIRED_VERSION} REQUIRED COMPONENTS chrono iostreams program_options)

if(STATIC_BUILD_DO_NOT_USE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

find_package(PkgConfig)

if(APPLE)
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

pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto>=${LIBCRYPTO_REQUIRED_VERSION})
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive>=${LIBARCHIVE_REQUIRED_VERSION})
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash>=${XXHASH_REQUIRED_VERSION})
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd>=${ZSTD_REQUIRED_VERSION})
pkg_check_modules(LIBLZ4 IMPORTED_TARGET liblz4>=${LIBLZ4_REQUIRED_VERSION})
pkg_check_modules(LIBLZMA IMPORTED_TARGET liblzma>=${LIBLZMA_REQUIRED_VERSION})
pkg_check_modules(LIBBROTLIDEC IMPORTED_TARGET libbrotlidec>=${LIBBROTLI_REQUIRED_VERSION})
pkg_check_modules(LIBBROTLIENC IMPORTED_TARGET libbrotlienc>=${LIBBROTLI_REQUIRED_VERSION})
# pkg_check_modules(LIBMAGIC IMPORTED_TARGET libmagic>=${LIBMAGIC_REQUIRED_VERSION})
if(TRY_ENABLE_FLAC)
  pkg_check_modules(FLAC IMPORTED_TARGET flac++>=${FLAC_REQUIRED_VERSION})
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

if(USE_JEMALLOC)
  pkg_check_modules(JEMALLOC IMPORTED_TARGET jemalloc>=${JEMALLOC_REQUIRED_VERSION})
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  foreach(CompilerFlag CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REPLACE "/RTC1" "" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()

  set(MSVC_USE_STATIC_RUNTIME ON CACHE BOOL "static build")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/folly.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/thrift.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/thrift_library.cmake)

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
  # TODO: support FetchContent
  add_subdirectory(ricepp)
endif()

add_library(
  dwarfs_common

  src/block_compressor.cpp
  src/block_compressor_parser.cpp
  src/checksum.cpp
  src/conv.cpp
  src/error.cpp
  src/file_access_generic.cpp
  src/file_stat.cpp
  src/file_util.cpp
  src/fstypes.cpp
  src/history.cpp
  src/library_dependencies.cpp
  src/logger.cpp
  src/mmap.cpp
  src/option_map.cpp
  src/options.cpp
  src/os_access_generic.cpp
  src/pcm_sample_transformer.cpp
  src/performance_monitor.cpp
  src/terminal_ansi.cpp
  src/thread_pool.cpp
  src/util.cpp
  src/xattr.cpp

  src/internal/features.cpp
  src/internal/file_status_conv.cpp
  src/internal/fs_section.cpp
  src/internal/string_table.cpp
  src/internal/wcwidth.c
  src/internal/worker_group.cpp

  src/xattr_$<IF:$<BOOL:${WIN32}>,win,posix>.cpp

  $<IF:${DWARFS_GIT_BUILD},${CMAKE_CURRENT_BINARY_DIR},${CMAKE_CURRENT_SOURCE_DIR}>/src/version.cpp

  src/compression/null.cpp
  src/compression/zstd.cpp
  $<$<BOOL:${LIBLZMA_FOUND}>:src/compression/lzma.cpp>
  $<$<BOOL:${LIBLZ4_FOUND}>:src/compression/lz4.cpp>
  $<$<AND:$<BOOL:${LIBBROTLIDEC_FOUND}>,$<BOOL:${LIBBROTLIENC_FOUND}>>:src/compression/brotli.cpp>
  $<$<BOOL:${FLAC_FOUND}>:src/compression/flac.cpp>
  $<$<BOOL:${ENABLE_RICEPP}>:src/compression/ricepp.cpp>
)

add_library(
  dwarfs_reader

  src/reader/block_range.cpp
  src/reader/filesystem_v2.cpp
  src/reader/metadata_types.cpp

  src/reader/internal/block_cache.cpp
  src/reader/internal/cached_block.cpp
  src/reader/internal/filesystem_parser.cpp
  src/reader/internal/inode_reader_v2.cpp
  src/reader/internal/metadata_types.cpp
  src/reader/internal/metadata_v2.cpp
)

add_library(
  dwarfs_writer

  src/writer/categorizer.cpp
  src/writer/category_parser.cpp
  src/writer/chmod_entry_transformer.cpp
  src/writer/compression_metadata_requirements.cpp
  src/writer/console_writer.cpp
  src/writer/entry_factory.cpp
  src/writer/filesystem_block_category_resolver.cpp
  src/writer/filesystem_writer.cpp
  src/writer/filter_debug.cpp
  src/writer/fragment_category.cpp
  src/writer/fragment_order_parser.cpp
  src/writer/inode_fragments.cpp
  src/writer/rule_based_entry_filter.cpp
  src/writer/scanner.cpp
  src/writer/segmenter.cpp
  src/writer/segmenter_factory.cpp
  src/writer/writer_progress.cpp

  src/writer/internal/block_manager.cpp
  src/writer/internal/chmod_transformer.cpp
  src/writer/internal/entry.cpp
  src/writer/internal/file_scanner.cpp
  src/writer/internal/fragment_chunkable.cpp
  src/writer/internal/global_entry_data.cpp
  src/writer/internal/inode_element_view.cpp
  src/writer/internal/inode_manager.cpp
  src/writer/internal/inode_ordering.cpp
  src/writer/internal/metadata_freezer.cpp
  src/writer/internal/nilsimsa.cpp
  src/writer/internal/progress.cpp
  src/writer/internal/scanner_progress.cpp
  src/writer/internal/similarity.cpp
  src/writer/internal/similarity_ordering.cpp

  # src/writer/categorizer/binary_categorizer.cpp
  src/writer/categorizer/fits_categorizer.cpp
  src/writer/categorizer/incompressible_categorizer.cpp
  src/writer/categorizer/pcmaudio_categorizer.cpp

  # $<$<BOOL:${LIBMAGIC_FOUND}>:src/writer/categorizer/libmagic_categorizer.cpp>
)

add_library(
  dwarfs_rewrite

  src/utility/rewrite_filesystem.cpp
)

add_library(
  dwarfs_extractor

  src/utility/filesystem_extractor.cpp
)

add_library(
  dwarfs_tool OBJECT

  src/tool/iolayer.cpp
  src/tool/main_adapter.cpp
  src/tool/safe_main.cpp
  src/tool/sys_char.cpp
  src/tool/tool.cpp
)

if(WITH_MAN_OPTION)
  target_sources(dwarfs_tool PRIVATE
    src/tool/pager.cpp
    src/tool/render_manpage.cpp
  )
endif()

target_link_libraries(dwarfs_common PRIVATE dwarfs_folly_lite PkgConfig::LIBCRYPTO PkgConfig::XXHASH)
target_link_libraries(dwarfs_reader PUBLIC dwarfs_common)
target_link_libraries(dwarfs_writer PUBLIC dwarfs_common)
target_link_libraries(dwarfs_extractor PUBLIC dwarfs_reader)
target_link_libraries(dwarfs_rewrite PUBLIC dwarfs_reader dwarfs_writer)
target_link_libraries(dwarfs_tool PUBLIC dwarfs_common)

if(DWARFS_GIT_BUILD)
  target_include_directories(dwarfs_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  )
endif()

target_compile_definitions(
  dwarfs_tool PRIVATE DWARFS_BUILD_ID="${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SYSTEM}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

target_compile_definitions(
  dwarfs_common PRIVATE
  DWARFS_SYSTEM_ID="${CMAKE_SYSTEM} [${CMAKE_SYSTEM_PROCESSOR}]"
  DWARFS_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

if(ENABLE_RICEPP)
  target_link_libraries(dwarfs_common PRIVATE ${RICEPP_OBJECT_TARGETS})
endif()

if(STATIC_BUILD_DO_NOT_USE)
  add_link_options(-static -static-libgcc)
endif(STATIC_BUILD_DO_NOT_USE)

foreach(tgt mkdwarfs dwarfsck dwarfsextract)
  add_library(${tgt}_main OBJECT tools/src/${tgt}_main.cpp)

  add_executable(${tgt} tools/src/${tgt}.cpp)

  list(APPEND BINARY_TARGETS ${tgt})
endforeach()

if(WITH_UNIVERSAL_BINARY)
  add_executable(dwarfsuniversal tools/src/universal.cpp)
  list(APPEND BINARY_TARGETS dwarfsuniversal)

  target_link_libraries(dwarfsuniversal PRIVATE
          mkdwarfs_main dwarfsck_main dwarfsextract_main)
  set_target_properties(dwarfsuniversal PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY universal
          OUTPUT_NAME dwarfs-universal)
endif()

if(WITH_PXATTR)
  add_executable(pxattr tools/src/pxattr.cpp)
  list(APPEND BINARY_TARGETS pxattr)
endif()

install(TARGETS mkdwarfs dwarfsck dwarfsextract RUNTIME DESTINATION bin)

list(APPEND MAIN_TARGETS mkdwarfs_main dwarfsck_main dwarfsextract_main)

if(FUSE3_FOUND OR WINFSP OR APPLE)
  add_library(dwarfs_main OBJECT tools/src/dwarfs_main.cpp)
  target_compile_definitions(dwarfs_main PRIVATE _FILE_OFFSET_BITS=64)
  add_executable(dwarfs-bin tools/src/dwarfs.cpp)
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
  add_library(dwarfs2_main tools/src/dwarfs_main.cpp)
  target_compile_definitions(dwarfs2_main PRIVATE _FILE_OFFSET_BITS=64
                                                  FUSE_USE_VERSION=29)
  target_link_libraries(dwarfs2_main PRIVATE PkgConfig::FUSE)
  add_executable(dwarfs2-bin tools/src/dwarfs.cpp)
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

if(WITH_MAN_OPTION)
  if(DWARFS_GIT_BUILD)
    include(${CMAKE_SOURCE_DIR}/cmake/render_manpage.cmake)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/src")
    set(DWARFS_MANPAGE_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(DWARFS_MANPAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  foreach(man mkdwarfs dwarfs dwarfsck dwarfsextract)
    if(DWARFS_GIT_BUILD)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools/src")
      add_manpage_source(doc/${man}.md NAME ${man}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tools/src/${man}_manpage.cpp)
    endif()

    if(TARGET ${man}_main)
      target_sources(${man}_main PRIVATE ${DWARFS_MANPAGE_SOURCE_DIR}/tools/src/${man}_manpage.cpp)
    endif()
  endforeach()

  if(TARGET dwarfs2_main)
    target_sources(dwarfs2_main PRIVATE ${DWARFS_MANPAGE_SOURCE_DIR}/tools/src/dwarfs_manpage.cpp)
  endif()
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
    filesystem_writer_test
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
    file_utils_test
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
    mkdwarfs_main dwarfsck_main dwarfsextract_main)

  if(TARGET manpage_test)
    target_link_libraries(manpage_test PRIVATE mkdwarfs_main dwarfsck_main dwarfsextract_main)
    if (TARGET dwarfs_main)
      target_link_libraries(manpage_test PRIVATE dwarfs_main)
    endif()
  endif()

  target_link_libraries(block_cache_test PRIVATE mkdwarfs_main)

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

target_link_libraries(dwarfs_common PRIVATE dwarfs_thrift_lite)

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

set(DWARFS_HAVE_LIBZSTD ON)
set(DWARFS_USE_JEMALLOC ${USE_JEMALLOC})
set(DWARFS_HAVE_RICEPP ${ENABLE_RICEPP})
set(DWARFS_HAVE_LIBMAGIC ${LIBMAGIC_FOUND})
set(DWARFS_HAVE_LIBLZ4 ${LIBLZ4_FOUND})
set(DWARFS_HAVE_LIBLZMA ${LIBLZMA_FOUND})
set(DWARFS_HAVE_LIBBROTLI ${LIBBROTLIDEC_FOUND} AND ${LIBBROTLIENC_FOUND})
set(DWARFS_HAVE_FLAC ${FLAC_FOUND})
set(DWARFS_BUILTIN_MANPAGE ${WITH_MAN_OPTION})
set(DWARFS_PERFMON_ENABLED ${ENABLE_PERFMON})
set(DWARFS_STACKTRACE_ENABLED ${ENABLE_STACKTRACE})
configure_file(cmake/config.h.in include/dwarfs/config.h @ONLY)

foreach(tgt ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  target_include_directories(${tgt} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/include>)
endforeach()

foreach(tgt dwarfs_common dwarfs_reader dwarfs_writer
            dwarfs_extractor dwarfs_rewrite dwarfs_tool
            ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  set_target_properties(${tgt} PROPERTIES EXPORT_COMPILE_COMMANDS ON)

  target_link_libraries(${tgt} PUBLIC Boost::boost)

  if(USE_JEMALLOC)
    target_link_libraries(${tgt} PRIVATE PkgConfig::JEMALLOC)
  endif(USE_JEMALLOC)

  target_include_directories(${tgt} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )

  target_include_directories(${tgt} PRIVATE
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:phmap,INTERFACE_INCLUDE_DIRECTORIES>>
  )

  target_include_directories(${tgt} PUBLIC
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:range-v3::range-v3,INTERFACE_INCLUDE_DIRECTORIES>>
  )

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

  if(DWARFS_FMT_LIB)
    target_link_libraries(${tgt} PRIVATE ${DWARFS_FMT_LIB})
  endif()

  if(WIN32)
    target_link_libraries(${tgt} PRIVATE ntdll.lib dbghelp.lib)
  endif()
endforeach()

if(WIN32)
  target_link_libraries(dwarfs_common PRIVATE bcrypt.lib)
endif()

if(CMAKE_BUILD_TYPE STREQUAL Release)
  # not sure why exactly, copied from fsst/CMakeLists.txt
  set_source_files_properties(fsst/fsst_avx512.cpp PROPERTIES COMPILE_FLAGS -O1)
endif()

add_library(
  dwarfs_fsst OBJECT
  fsst/libfsst.cpp fsst/fsst_avx512.cpp fsst/fsst_avx512_unroll1.inc
  fsst/fsst_avx512_unroll2.inc fsst/fsst_avx512_unroll3.inc
  fsst/fsst_avx512_unroll4.inc)

target_include_directories(dwarfs_common SYSTEM PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fsst>)
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

foreach(tgt dwarfs_common dwarfs_reader dwarfs_writer dwarfs_tool
            dwarfs_extractor dwarfs_rewrite dwarfs_fsst dwarfs_test_helpers
            dwarfs_folly_lite dwarfs_thrift_lite dwarfs_follybenchmark_lite
            dwarfs_compression_thrift dwarfs_metadata_thrift
            dwarfs_history_thrift dwarfs_features_thrift
            ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
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

# if(LIBMAGIC_FOUND)
#   target_link_libraries(dwarfs PkgConfig::LIBMAGIC)
# endif()

target_link_libraries(dwarfs_common PRIVATE PkgConfig::ZSTD)
target_link_libraries(dwarfs_writer PRIVATE PkgConfig::ZSTD)

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

target_link_libraries(mkdwarfs_main PRIVATE dwarfs_reader dwarfs_writer dwarfs_rewrite)
target_link_libraries(dwarfsck_main PRIVATE dwarfs_reader)
target_link_libraries(dwarfsextract_main PRIVATE dwarfs_extractor)

target_link_libraries(mkdwarfs PRIVATE mkdwarfs_main)
target_link_libraries(dwarfsck PRIVATE dwarfsck_main)
target_link_libraries(dwarfsextract PRIVATE dwarfsextract_main)

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
  target_link_libraries(${tgt} PRIVATE dwarfs_writer dwarfs_rewrite dwarfs_extractor)
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
endforeach()

foreach(tgt ${BINARY_TARGETS} ${TEST_TARGETS})
  list(APPEND REALCLEAN_FILES $<TARGET_FILE:${tgt}>.manifest)
  if(WIN32)
    list(APPEND REALCLEAN_FILES ${tgt}.ilk ${tgt}.pdb)
  endif()
endforeach()

add_custom_target(
  realclean
  COMMAND ${CMAKE_MAKE_PROGRAM} clean
  COMMAND ${CMAKE_COMMAND} -E rm -rf
      cmake_install.cmake install_manifest.txt
      dwarfs_install.cmake package_version.cmake
      CPackConfig.cmake CPackSourceConfig.cmake _CPack_Packages
      CTestTestfile.cmake Testing
      fbthrift folly zstd ricepp tools
      include src thrift universal bin lib man1 man5
      vcpkg-manifest-install.log
      Makefile compile_commands.json
      artifacts.env default.profraw profile
      dwarfs-config-version.cmake
      dwarfs-config.cmake
      dwarfs.ilk dwarfs.pdb
      .ninja_deps build.ninja
      CMakeCache.txt
      ${REALCLEAN_FILES}
)

add_custom_target(
  distclean
  COMMAND ${CMAKE_MAKE_PROGRAM} realclean
  COMMAND ${CMAKE_COMMAND} -E rm -rf _deps
  COMMAND ${CMAKE_COMMAND} -E rm -rf CMakeFiles
)

file(GLOB_RECURSE ALL_SOURCES LIST_DIRECTORIES false
        ${CMAKE_CURRENT_SOURCE_DIR}/ricepp/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/ricepp/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp)

add_custom_target(
  format
  COMMAND clang-format -i ${ALL_SOURCES})

if(NOT STATIC_BUILD_DO_NOT_USE)
  foreach(tgt dwarfs_common dwarfs_reader dwarfs_writer dwarfs_extractor dwarfs_rewrite)
    set_target_properties(${tgt} PROPERTIES VERSION ${PRJ_VERSION_MAJOR}.${PRJ_VERSION_MINOR}.${PRJ_VERSION_PATCH})
  endforeach()

  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set(DWARFS_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/dwarfs CACHE STRING
      "CMake package config files install location")

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dwarfs-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dwarfs-config.cmake
    INSTALL_DESTINATION ${DWARFS_CMAKE_INSTALL_DIR}
    PATH_VARS
      CMAKE_INSTALL_INCLUDEDIR
      DWARFS_CMAKE_INSTALL_DIR
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dwarfs-config-version.cmake
    VERSION ${PRJ_VERSION_MAJOR}.${PRJ_VERSION_MINOR}.${PRJ_VERSION_PATCH}
    COMPATIBILITY AnyNewerVersion
  )

  install(
    TARGETS dwarfs_common
            dwarfs_reader
            dwarfs_writer
            dwarfs_extractor
            dwarfs_rewrite
            dwarfs_tool

            # object libs
            dwarfs_folly_lite
            dwarfs_thrift_lite
            dwarfs_compression_thrift
            dwarfs_metadata_thrift
            dwarfs_history_thrift
            dwarfs_features_thrift
            dwarfs_fsst

            # other
            ${RICEPP_OBJECT_TARGETS}
            folly_deps
    EXPORT dwarfs-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

  install(
    DIRECTORY include/dwarfs
    DESTINATION include
    PATTERN include/dwarfs/internal EXCLUDE
    PATTERN include/dwarfs/tool EXCLUDE
    PATTERN include/dwarfs/*/internal EXCLUDE
  )

  if(DWARFS_GIT_BUILD)
    install(
      FILES ${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs/version.h
      DESTINATION include/dwarfs
    )
  endif()

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs/config.h
    DESTINATION include/dwarfs
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/dwarfs-config.cmake
    DESTINATION ${DWARFS_CMAKE_INSTALL_DIR}
  )

  install(
    EXPORT dwarfs-targets
    FILE dwarfs-targets.cmake
    NAMESPACE dwarfs::
    DESTINATION ${DWARFS_CMAKE_INSTALL_DIR}
  )
endif()

if(STATIC_BUILD_DO_NOT_USE OR APPLE)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(tgt ${BINARY_TARGETS})
      list(APPEND FILES_TO_STRIP $<TARGET_FILE:${tgt}>)
    endforeach()
    if(APPLE)
      add_custom_target(strip COMMAND strip ${FILES_TO_STRIP})
    else()
      add_custom_target(strip COMMAND strip $<IF:$<BOOL:${ENABLE_STACKTRACE}>,--strip-debug,--strip-all> ${FILES_TO_STRIP})
    endif()
  endif()
endif()

set(DWARFS_ARTIFACT_ID "${PRJ_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
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

if(STATIC_BUILD_DO_NOT_USE OR WIN32)
  if(WITH_UNIVERSAL_BINARY)
    set(UNIVERSAL_OUT "dwarfs-universal-${DWARFS_ARTIFACT_ID}${CMAKE_EXECUTABLE_SUFFIX}")

    if(ENABLE_STACKTRACE)
      message(WARNING "UPX compression is disabled with ENABLE_STACKTRACE")

      add_custom_command(
        OUTPUT ${UNIVERSAL_OUT}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dwarfsuniversal> ${UNIVERSAL_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      )
    else()
      find_program(UPX_EXE upx upx.exe PATHS "c:/bin" DOC "ultimate packer for executables" REQUIRED)

      # upx -9 is a good compromise between compression ratio and speed
      # also, anything above --best increases the startup time of the compressed
      # executable significantly
      add_custom_command(
        OUTPUT ${UNIVERSAL_OUT}
        COMMAND ${UPX_EXE} -9 -o ${UNIVERSAL_OUT} $<TARGET_FILE:dwarfsuniversal>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      )
    endif()

    add_custom_target(universal_upx DEPENDS ${UNIVERSAL_OUT})
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
               "universal_binary=${UNIVERSAL_OUT}\n")

    set(RICEPP_BENCHMARK_FITS "ricepp/ricepp_benchmark_fits")

    add_custom_target(copy_artifacts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}${PACKAGE_EXT} ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${UNIVERSAL_OUT} ${ARTIFACTS_FULL_PATH}
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
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "dwarfs - A high compression read-only file system")
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

include(CPack)
