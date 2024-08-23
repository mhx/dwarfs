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

option(WITH_LIBDWARFS "build with libdwarfs" ON)
option(WITH_TOOLS "build with tools" ON)
option(WITH_FUSE_DRIVER "build with FUSE driver" ON)
option(WITH_TESTS "build with tests" OFF)
option(WITH_BENCHMARKS "build with benchmarks" OFF)
option(WITH_FUZZ "build with fuzzing binaries" OFF)
option(WITH_MAN_OPTION "build with --man option" ON)
option(ENABLE_PERFMON "enable performance monitor in all tools" ON)
option(TRY_ENABLE_FLAC "build with FLAC support" ON)
option(ENABLE_RICEPP "build with RICEPP compression support" ON)
option(WITH_UNIVERSAL_BINARY "build with universal binary" OFF)
option(WITH_PXATTR "build with pxattr binary" OFF)
option(WITH_EXAMPLE "build with example binary" OFF)
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

if(STATIC_BUILD_DO_NOT_USE AND NOT(WITH_LIBDWARFS AND WITH_TOOLS AND WITH_FUSE_DRIVER))
  message(FATAL_ERROR "STATIC_BUILD_DO_NOT_USE requires WITH_LIBDWARFS, WITH_TOOLS and WITH_FUSE_DRIVER")
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
set(LIBFMT_PREFERRED_VERSION 11.0.2)

set(GOOGLETEST_REQUIRED_VERSION 1.13.0)
set(GOOGLETEST_PREFERRED_VERSION 1.15.2)

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

include(${CMAKE_SOURCE_DIR}/cmake/compile.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/version.cmake)

if(NOT PRJ_VERSION_FULL)
  message(FATAL_ERROR "PRJ_VERSION_FULL is not set")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules")

find_package(PkgConfig)

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

  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

if(WITH_MAN_PAGES)
  include("${CMAKE_SOURCE_DIR}/cmake/manpage.cmake")
  if(WITH_LIBDWARFS)
    list(APPEND DWARFS_MANPAGES dwarfs-format.5)
  endif()
  if(WITH_TOOLS)
    list(APPEND DWARFS_MANPAGES mkdwarfs.1 dwarfsck.1 dwarfsextract.1)
  endif()
  if(WITH_FUSE_DRIVER)
    list(APPEND DWARFS_MANPAGES dwarfs.1)
  endif()
  foreach(man ${DWARFS_MANPAGES})
    add_manpage(${man})
  endforeach()
endif()

include(FetchContent)

if(WITH_TESTS)
  include(${CMAKE_SOURCE_DIR}/cmake/need_gtest.cmake)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/need_fmt.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/need_range_v3.cmake)

if(WITH_LIBDWARFS)
  include(${CMAKE_SOURCE_DIR}/cmake/need_phmap.cmake)

  find_package(Boost ${BOOST_REQUIRED_VERSION} REQUIRED CONFIG
               COMPONENTS chrono iostreams program_options
               OPTIONAL_COMPONENTS process)

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

  if(USE_JEMALLOC)
    pkg_check_modules(JEMALLOC IMPORTED_TARGET jemalloc>=${JEMALLOC_REQUIRED_VERSION})
  endif()

  include(${CMAKE_SOURCE_DIR}/cmake/thrift_library.cmake)

  include(${CMAKE_SOURCE_DIR}/cmake/folly.cmake)
  include(${CMAKE_SOURCE_DIR}/cmake/thrift.cmake)

  if(ENABLE_RICEPP)
    # TODO: support FetchContent
    add_subdirectory(ricepp)
  endif()

  include(${CMAKE_SOURCE_DIR}/cmake/libdwarfs.cmake)

  set(DWARFS_HAVE_LIBZSTD ON)
  set(DWARFS_USE_JEMALLOC ${USE_JEMALLOC})
  set(DWARFS_HAVE_RICEPP ${ENABLE_RICEPP})
  set(DWARFS_HAVE_LIBMAGIC ${LIBMAGIC_FOUND})
  set(DWARFS_HAVE_LIBLZ4 ${LIBLZ4_FOUND})
  set(DWARFS_HAVE_LIBLZMA ${LIBLZMA_FOUND})
  if(${LIBBROTLIDEC_FOUND} AND ${LIBBROTLIENC_FOUND})
    set(DWARFS_HAVE_LIBBROTLI ON)
  else()
    set(DWARFS_HAVE_LIBBROTLI OFF)
  endif()
  set(DWARFS_HAVE_FLAC ${FLAC_FOUND})
  set(DWARFS_BUILTIN_MANPAGE ${WITH_MAN_OPTION})
  set(DWARFS_PERFMON_ENABLED ${ENABLE_PERFMON})
  set(DWARFS_STACKTRACE_ENABLED ${ENABLE_STACKTRACE})

  configure_file(cmake/config.h.in include/dwarfs/config.h @ONLY)
else()
  find_package(dwarfs ${PRJ_VERSION_MAJOR}.${PRJ_VERSION_MINOR}.${PRJ_VERSION_PATCH} EXACT REQUIRED CONFIG)
  add_library(dwarfs_common ALIAS dwarfs::dwarfs_common)
  add_library(dwarfs_reader ALIAS dwarfs::dwarfs_reader)
  add_library(dwarfs_writer ALIAS dwarfs::dwarfs_writer)
  add_library(dwarfs_rewrite ALIAS dwarfs::dwarfs_rewrite)
  add_library(dwarfs_extractor ALIAS dwarfs::dwarfs_extractor)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/libdwarfs_tool.cmake)

if(WITH_TOOLS)
  foreach(tgt mkdwarfs dwarfsck dwarfsextract)
    add_library(${tgt}_main OBJECT tools/src/${tgt}_main.cpp)
    add_executable(${tgt} tools/src/${tgt}.cpp)

    target_link_libraries(${tgt} PRIVATE ${tgt}_main)

    list(APPEND MAIN_TARGETS ${tgt}_main)
    list(APPEND BINARY_TARGETS ${tgt})

    install(TARGETS ${tgt} RUNTIME DESTINATION bin)
  endforeach()

  target_link_libraries(mkdwarfs_main PRIVATE dwarfs_reader dwarfs_writer dwarfs_rewrite)
  target_link_libraries(dwarfsck_main PRIVATE dwarfs_reader)
  target_link_libraries(dwarfsextract_main PRIVATE dwarfs_extractor)

  if(WITH_UNIVERSAL_BINARY)
    add_executable(dwarfsuniversal tools/src/universal.cpp)
    list(APPEND BINARY_TARGETS dwarfsuniversal)

    target_link_libraries(dwarfsuniversal PRIVATE
            mkdwarfs_main dwarfsck_main dwarfsextract_main)
    set_target_properties(dwarfsuniversal PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY universal
            OUTPUT_NAME dwarfs-universal)
  endif()
endif()

if(WITH_PXATTR)
  add_executable(pxattr tools/src/pxattr.cpp)
  list(APPEND BINARY_TARGETS pxattr)
  install(TARGETS pxattr RUNTIME DESTINATION bin)
endif()

if(WITH_EXAMPLE)
  add_executable(example example/example.cpp)
  target_link_libraries(example PRIVATE dwarfs_reader dwarfs_extractor)
  list(APPEND BINARY_TARGETS example)
endif()

if(WITH_FUSE_DRIVER)
  include(${CMAKE_SOURCE_DIR}/cmake/need_fuse.cmake)

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
    target_link_libraries(dwarfs_main PRIVATE dwarfs_reader)
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
    target_link_libraries(dwarfs2_main PRIVATE dwarfs_reader)
  endif()
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
    test/loremipsum.cpp
    test/test_dirtree.cpp
    test/filter_test_data.cpp
  )
  if(WITH_BENCHMARKS)
    target_sources(dwarfs_test_helpers PRIVATE test/test_strings.cpp)
  endif()
  target_link_libraries(dwarfs_test_helpers PUBLIC dwarfs_common dwarfs_writer dwarfs_tool)
  set_property(TARGET dwarfs_test_helpers PROPERTY CXX_STANDARD 20)
endif()

if(WITH_TESTS)
  if(WITH_LIBDWARFS)
    list(APPEND DWARFS_TESTS
      badfs_test
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
      packed_int_vector_test
      packed_ptr_test
      pcm_sample_transformer_test
      pcmaudio_categorizer_test
      speedometer_test
      terminal_test
      utils_test
      file_utils_test
      worker_group_test
    )

    if(FLAC_FOUND)
      list(APPEND DWARFS_TESTS
        flac_compressor_test
      )
    endif()
  endif()

  if(WITH_TOOLS)
    list(APPEND DWARFS_TESTS
      block_cache_test
      tool_main_test
    )
  endif()

  if(WITH_TOOLS OR WITH_FUSE_DRIVER)
    if(NOT WITH_TOOLS)
      find_program(MKDWARFS_EXE mkdwarfs mkdwarfs.exe)
      find_program(DWARFSCK_EXE dwarfsck dwarfsck.exe)
      find_program(DWARFSEXTRACT_EXE dwarfsextract dwarfsextract.exe)
    endif()
    if(WITH_TOOLS OR (MKDWARFS_EXE AND DWARFSCK_EXE AND DWARFSEXTRACT_EXE))
      list(APPEND DWARFS_TESTS
        tools_test
      )
    endif()
  endif()

  if((WITH_TOOLS OR WITH_FUSE_DRIVER) AND WITH_MAN_OPTION)
    list(APPEND DWARFS_TESTS manpage_test)
  endif()

  if(WITH_LIBDWARFS AND ENABLE_RICEPP)
    list(APPEND DWARFS_TESTS ricepp_compressor_test)
  endif()

  foreach (test ${DWARFS_TESTS})
    add_executable(${test} test/${test}.cpp)
    target_link_libraries(
      ${test} PRIVATE dwarfs_test_helpers gmock gtest gtest_main
    )

    if(NOT PREFER_SYSTEM_GTEST)
      ### This is a wild hack. At least on macOS, gtest and basically everything
      ### Homebrew is installed in /usr/local, and /usr/local/include can end up
      ### in the compiler's include path *before* the include path of our local
      ### gtest/gmock. The following code tries to ensure that the gtest/gmock
      ### include paths are searched first.
      get_target_property(gmock_include_dirs gmock INTERFACE_INCLUDE_DIRECTORIES)
      get_target_property(gtest_include_dirs gtest INTERFACE_INCLUDE_DIRECTORIES)
      target_include_directories(${test} PRIVATE BEFORE ${gmock_include_dirs} ${gtest_include_dirs})
    endif()

    target_compile_definitions(${test}
      PRIVATE TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/test\"
              TOOLS_BIN_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\")
    list(APPEND TEST_TARGETS ${test})
  endforeach()

  if(TARGET tool_main_test)
    target_link_libraries(tool_main_test PRIVATE mkdwarfs_main dwarfsck_main dwarfsextract_main)
  endif()

  if(TARGET manpage_test)
    if(WITH_TOOLS)
      target_compile_definitions(manpage_test PRIVATE DWARFS_WITH_TOOLS)
      target_link_libraries(manpage_test PRIVATE mkdwarfs_main dwarfsck_main dwarfsextract_main)
    endif()
    if(WITH_FUSE_DRIVER)
      target_compile_definitions(manpage_test PRIVATE DWARFS_WITH_FUSE_DRIVER)
      target_link_libraries(manpage_test PRIVATE dwarfs_main)
    endif()
  endif()

  if(TARGET tools_test)
    target_compile_definitions(tools_test PRIVATE
      $<$<AND:$<BOOL:${WITH_UNIVERSAL_BINARY}>,$<BOOL:${WITH_TOOLS}>>:DWARFS_HAVE_UNIVERSAL_BINARY>
      $<$<BOOL:${WITH_TOOLS}>:DWARFS_WITH_TOOLS>
      $<$<BOOL:${WITH_FUSE_DRIVER}>:DWARFS_WITH_FUSE_DRIVER>
      $<$<BOOL:${MKDWARFS_EXE}>:MKDWARFS_BINARY=\"${MKDWARFS_EXE}\">
      $<$<BOOL:${DWARFSCK_EXE}>:DWARFSCK_BINARY=\"${DWARFSCK_EXE}\">
      $<$<BOOL:${DWARFSEXTRACT_EXE}>:DWARFSEXTRACT_BINARY=\"${DWARFSEXTRACT_EXE}\">
    )
  endif()

  if(TARGET block_cache_test)
    target_link_libraries(block_cache_test PRIVATE mkdwarfs_main)
  endif()

  foreach(tgt fits_categorizer_test
              incompressible_categorizer_test
              pcmaudio_categorizer_test
              tool_main_test)
    if(TARGET ${tgt})
      target_link_libraries(${tgt} PRIVATE dwarfs_writer)
    endif()
  endforeach()

  foreach(tgt ${TEST_TARGETS})
    gtest_discover_tests(${tgt}
      DISCOVERY_TIMEOUT 120
      PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profile/%32m.profraw"
    )
  endforeach()
endif()

if(WITH_LIBDWARFS AND WITH_BENCHMARKS)
  find_package(benchmark 1.8)
  if(benchmark_FOUND)
    add_executable(dwarfs_benchmark test/dwarfs_benchmark.cpp)
    target_link_libraries(dwarfs_benchmark PRIVATE dwarfs_test_helpers benchmark::benchmark)
    target_link_libraries(dwarfs_benchmark PRIVATE dwarfs_reader dwarfs_writer)
    list(APPEND BENCHMARK_TARGETS dwarfs_benchmark)

    add_executable(multiversioning_benchmark test/multiversioning_benchmark.cpp)
    target_link_libraries(multiversioning_benchmark PRIVATE benchmark::benchmark)
    target_link_libraries(multiversioning_benchmark PRIVATE dwarfs_writer)
    list(APPEND BENCHMARK_TARGETS multiversioning_benchmark)

    add_executable(converter_benchmark test/converter_benchmark.cpp)
    target_link_libraries(converter_benchmark PRIVATE dwarfs_test_helpers benchmark::benchmark)
    list(APPEND BENCHMARK_TARGETS converter_benchmark)
  endif()

  # TODO: migrate to benchmark?
  add_executable(segmenter_benchmark test/segmenter_benchmark.cpp)
  target_link_libraries(segmenter_benchmark PRIVATE dwarfs_follybenchmark_lite dwarfs_test_helpers)
  target_link_libraries(segmenter_benchmark PRIVATE dwarfs_writer)
  list(APPEND BENCHMARK_TARGETS segmenter_benchmark)

  list(APPEND BINARY_TARGETS ${BENCHMARK_TARGETS})
endif()

if(WITH_LIBDWARFS AND WITH_FUZZ)
  add_executable(fuzz_categorizers test/fuzz_categorizers.cpp)
  target_link_libraries(fuzz_categorizers PRIVATE dwarfs_writer)
  list(APPEND BINARY_TARGETS fuzz_categorizers)

  add_executable(fuzz_mkdwarfs test/fuzz_mkdwarfs.cpp)
  target_link_libraries(fuzz_mkdwarfs PRIVATE mkdwarfs_main dwarfs_test_helpers)
  list(APPEND BINARY_TARGETS fuzz_mkdwarfs)
endif()

foreach(tgt ${LIBDWARFS_TARGETS})
  target_include_directories(${tgt} PRIVATE
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:phmap,INTERFACE_INCLUDE_DIRECTORIES>>
  )
endforeach()

foreach(tgt ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  target_include_directories(${tgt} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/include>)
endforeach()

foreach(tgt ${LIBDWARFS_TARGETS} ${LIBDWARFS_OBJECT_TARGETS} dwarfs_test_helpers
            ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  if(NOT TARGET ${tgt})
    continue()
  endif()

  set_target_properties(${tgt} PROPERTIES EXPORT_COMPILE_COMMANDS ON)

  target_link_libraries(${tgt} PUBLIC Boost::boost)

  # TODO: need to get USE_JEMALLOC and others from imported libdwarfs
  if(USE_JEMALLOC)
    target_link_libraries(${tgt} PRIVATE PkgConfig::JEMALLOC)
  endif(USE_JEMALLOC)

  target_include_directories(${tgt} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
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

if(CMAKE_BUILD_TYPE STREQUAL Release)
  # not sure why exactly, copied from fsst/CMakeLists.txt
  set_source_files_properties(fsst/fsst_avx512.cpp PROPERTIES COMPILE_FLAGS -O1)
endif()

foreach(tgt dwarfs_test_helpers dwarfs_follybenchmark_lite
            ${LIBDWARFS_TARGETS} ${LIBDWARFS_OBJECT_TARGETS}
            ${BINARY_TARGETS} ${TEST_TARGETS} ${MAIN_TARGETS})
  if(TARGET ${tgt})
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
  endif()
endforeach()

foreach(tgt ${MAIN_TARGETS} ${BINARY_TARGETS} ${TEST_TARGETS})
  target_link_libraries(${tgt} PRIVATE dwarfs_tool)
endforeach()

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
  if(STATIC_BUILD_DO_NOT_USE)
    target_link_options(${tgt} PRIVATE -static -static-libgcc)
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
      artifacts.env source-artifacts.env
      default.profraw profile
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
if(ENABLE_STACKTRACE)
  set(DWARFS_ARTIFACT_ID "${DWARFS_ARTIFACT_ID}-stacktrace")
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

  set(SOURCE_TARBALL "${CMAKE_PROJECT_NAME}-${PRJ_VERSION_FULL}${PACKAGE_EXT}")

  add_custom_target(copy_source_artifacts
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${SOURCE_TARBALL} ${ARTIFACTS_FULL_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DWARFS_ARTIFACTS_DIR}/cache
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../${ARTIFACTS_SUBDIR}/${SOURCE_TARBALL}
                ${DWARFS_ARTIFACTS_DIR}/cache/dwarfs-source-$ENV{GITHUB_RUN_NUMBER}${PACKAGE_EXT}
  )

  if(STATIC_BUILD_DO_NOT_USE OR WIN32)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/artifacts.env"
               "binary_tarball=${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}${PACKAGE_EXT}\n"
               "universal_binary=${UNIVERSAL_OUT}\n")

    add_custom_command(
      OUTPUT _copy_artifacts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-${DWARFS_ARTIFACT_ID}${PACKAGE_EXT} ${ARTIFACTS_FULL_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${UNIVERSAL_OUT} ${ARTIFACTS_FULL_PATH}
    )

    if(WITH_BENCHMARKS)
      foreach(tgt ${BENCHMARK_TARGETS})
        add_custom_command(
          OUTPUT _copy_artifacts APPEND
          COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_BINARY_DIR}/${tgt}${CMAKE_EXECUTABLE_SUFFIX}
              ${ARTIFACTS_FULL_PATH}/${tgt}-${DWARFS_ARTIFACT_ID}${CMAKE_EXECUTABLE_SUFFIX}
        )
      endforeach()

      if (ENABLE_RICEPP)
        add_custom_command(
          OUTPUT _copy_artifacts APPEND
          COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_BINARY_DIR}/ricepp/ricepp_benchmark_fits${CMAKE_EXECUTABLE_SUFFIX}
              ${ARTIFACTS_FULL_PATH}/ricepp_benchmark_fits-${DWARFS_ARTIFACT_ID}${CMAKE_EXECUTABLE_SUFFIX}
        )
      endif()
    endif()

    add_custom_target(copy_artifacts DEPENDS _copy_artifacts)
    set_source_files_properties(_copy_artifacts PROPERTIES SYMBOLIC ON)
  endif()
endif()

configure_file("${PROJECT_SOURCE_DIR}/cmake/dwarfs_install.cmake.in" dwarfs_install.cmake @ONLY)
set(CPACK_INSTALL_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/dwarfs_install.cmake")

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
