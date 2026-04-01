#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT
#

add_library(
  dwarfs_common

  src/byte_buffer.cpp
  src/checksum.cpp
  src/compression_registry.cpp
  src/conv.cpp
  src/detail/file_extent_info.cpp
  src/detail/scoped_env.cpp
  src/error.cpp
  src/extent_kind.cpp
  src/file_access_generic.cpp
  src/file_extent.cpp
  src/file_extents_iterable.cpp
  src/file_range_utils.cpp
  src/file_segments_iterable.cpp
  src/file_stat.cpp
  src/file_util.cpp
  src/fstypes.cpp
  src/glob_matcher.cpp
  src/history.cpp
  src/library_dependencies.cpp
  src/logger.cpp
  src/malloc_byte_buffer.cpp
  src/mapped_byte_buffer.cpp
  src/option_map.cpp
  src/os_access_generic.cpp
  src/pcm_sample_transformer.cpp
  $<IF:$<BOOL:${ENABLE_PERFMON}>,src/performance_monitor.cpp,>
  src/scoped_output_capture.cpp
  src/terminal_ansi.cpp
  src/thread_pool.cpp
  src/util.cpp
  src/varint.cpp
  src/xattr.cpp

  src/internal/features.cpp
  src/internal/file_status_conv.cpp
  src/internal/fs_section.cpp
  src/internal/fs_section_checker.cpp
  src/internal/fsst.cpp
  src/internal/glob_to_regex.cpp
  src/internal/malloc_buffer.cpp
  src/internal/mappable_file.cpp
  src/internal/io_ops_$<IF:$<BOOL:${WIN32}>,win,posix>.cpp
  src/internal/io_ops_helpers.cpp
  src/internal/metadata_utils.cpp
  src/internal/mmap_file_view.cpp
  src/internal/option_parser.cpp
  src/internal/os_access_generic_data.cpp
  src/internal/read_file_view.cpp
  src/internal/string_table.cpp
  src/internal/thread_util.cpp
  src/internal/unicode_case_folding.cpp
  src/internal/value_stream_quantile_estimator.cpp
  src/internal/wcwidth.c
  src/internal/worker_group.cpp

  src/xattr_$<IF:$<BOOL:${WIN32}>,win,posix>.cpp

  $<IF:${DWARFS_GIT_BUILD},${CMAKE_CURRENT_BINARY_DIR},${CMAKE_CURRENT_SOURCE_DIR}>/src/version.cpp

  src/compression/base.cpp
  src/compression/null.cpp
  src/compression/zstd.cpp
  $<$<BOOL:${LIBLZMA_FOUND}>:src/compression/lzma.cpp>
  $<$<BOOL:${LIBLZ4_FOUND}>:src/compression/lz4.cpp>
  $<$<AND:$<BOOL:${LIBBROTLIDEC_FOUND}>,$<BOOL:${LIBBROTLIENC_FOUND}>>:src/compression/brotli.cpp>
  $<$<BOOL:${FLAC_FOUND}>:src/compression/flac.cpp>
  $<$<BOOL:${ENABLE_RICEPP}>:src/compression/ricepp.cpp>
)

add_library(
  dwarfs_compressor

  src/block_compressor.cpp
  src/block_compressor_parser.cpp
  src/compressor_registry.cpp
)

add_library(
  dwarfs_decompressor

  src/block_decompressor.cpp
  src/decompressor_registry.cpp
)

add_library(
  dwarfs_reader

  src/reader/block_cache_options.cpp
  src/reader/block_range.cpp
  src/reader/detail/file_reader.cpp
  src/reader/filesystem_options.cpp
  src/reader/filesystem_v2.cpp
  src/reader/fsinfo_features.cpp
  src/reader/metadata_types.cpp
  src/reader/mlock_mode.cpp

  src/reader/internal/block_cache.cpp
  src/reader/internal/block_cache_byte_buffer_factory.cpp
  src/reader/internal/cached_block.cpp
  src/reader/internal/filesystem_parser.cpp
  src/reader/internal/inode_reader_v2.cpp
  src/reader/internal/metadata_analyzer.cpp
  src/reader/internal/metadata_types.cpp
  src/reader/internal/metadata_v2.cpp
  src/reader/internal/periodic_executor.cpp
  src/reader/internal/time_resolution_handler.cpp
)

add_library(
  dwarfs_writer

  src/writer/categorizer.cpp
  src/writer/category_parser.cpp
  src/writer/compression_metadata_requirements.cpp
  src/writer/console_writer.cpp
  src/writer/entry_factory.cpp
  src/writer/entry_tree.cpp
  src/writer/fragment_order_options.cpp
  src/writer/filesystem_block_category_resolver.cpp
  src/writer/filesystem_writer.cpp
  src/writer/filter_debug.cpp
  src/writer/fragment_category.cpp
  src/writer/fragment_order_parser.cpp
  src/writer/inode_fragments.cpp
  src/writer/metadata_options.cpp
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
  src/writer/internal/inode_hole_mapper.cpp
  src/writer/internal/inode_manager.cpp
  src/writer/internal/inode_ordering.cpp
  src/writer/internal/metadata_builder.cpp
  src/writer/internal/metadata_freezer.cpp
  src/writer/internal/nilsimsa.cpp
  src/writer/internal/progress.cpp
  src/writer/internal/scanner_progress.cpp
  src/writer/internal/similarity.cpp
  src/writer/internal/similarity_ordering.cpp
  src/writer/internal/time_resolution_converter.cpp

  src/writer/categorizer/binary_categorizer.cpp
  src/writer/categorizer/fits_categorizer.cpp
  src/writer/categorizer/hotness_categorizer.cpp
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
  src/utility/filesystem_extractor_archive_format.cpp
)

add_library(
  dwarfs_fsst OBJECT
  fsst/libfsst.cpp
  fsst/fsst_avx512.cpp
  fsst/fsst_avx512_unroll1.inc
  fsst/fsst_avx512_unroll2.inc
  fsst/fsst_avx512_unroll3.inc
  fsst/fsst_avx512_unroll4.inc
)

if(DWARFS_GIT_BUILD)
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
else()
  set(THRIFT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_library(
  dwarfs_frozen OBJECT

  frozen/thrift/lib/cpp2/frozen/Frozen.cpp
  frozen/thrift/lib/cpp2/frozen/FrozenUtil.cpp
  frozen/thrift/lib/cpp2/frozen/schema/MemorySchema.cpp

  ${THRIFT_GENERATED_DIR}/thrift/lib/thrift/gen-cpp-lite/frozen_types.cpp
)

target_include_directories(dwarfs_frozen PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/frozen>
  $<BUILD_INTERFACE:${THRIFT_GENERATED_DIR}>
)
target_include_directories(dwarfs_frozen SYSTEM PUBLIC
  $<BUILD_INTERFACE:$<TARGET_PROPERTY:phmap,INTERFACE_INCLUDE_DIRECTORIES>>
)
target_link_libraries(dwarfs_frozen PUBLIC PkgConfig::XXHASH)

add_library(
  dwarfs_thrift_lite_v2 OBJECT

  src/thrift_lite/assert.cpp
  src/thrift_lite/compact_reader.cpp
  src/thrift_lite/compact_writer.cpp
  src/thrift_lite/debug_writer.cpp
  src/thrift_lite/json_writer.cpp
  src/thrift_lite/varint.cpp

  src/thrift_lite/internal/compact_wire.cpp
)

target_link_libraries(dwarfs_thrift_lite_v2 PUBLIC dwarfs_frozen)
target_include_directories(dwarfs_frozen PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/frozen>
  $<BUILD_INTERFACE:${THRIFT_GENERATED_DIR}>
)

add_thrift_lite_library(thrift/metadata.thrift FROZEN
                        TARGET dwarfs_metadata_thrift OUTPUT_PATH dwarfs)
add_thrift_lite_library(thrift/compression.thrift
                        TARGET dwarfs_compression_thrift OUTPUT_PATH dwarfs)
add_thrift_lite_library(thrift/history.thrift
                        TARGET dwarfs_history_thrift OUTPUT_PATH dwarfs)
add_thrift_lite_library(thrift/features.thrift
                        TARGET dwarfs_features_thrift OUTPUT_PATH dwarfs)
add_thrift_lite_library(frozen/thrift/lib/thrift/frozen.thrift
                        OUTPUT_PATH lib/thrift NO_LIBRARY)

target_link_libraries(dwarfs_common PRIVATE PkgConfig::LIBCRYPTO PkgConfig::XXHASH zstd::preferred)
target_link_libraries(dwarfs_compressor PRIVATE dwarfs_common)
target_link_libraries(dwarfs_decompressor PRIVATE dwarfs_common)
target_link_libraries(dwarfs_reader PUBLIC dwarfs_common dwarfs_decompressor)
target_link_libraries(dwarfs_writer PUBLIC dwarfs_common dwarfs_compressor dwarfs_decompressor)
target_link_libraries(dwarfs_writer PRIVATE zstd::preferred)
target_link_libraries(dwarfs_extractor PUBLIC dwarfs_reader)
target_link_libraries(dwarfs_rewrite PUBLIC dwarfs_reader dwarfs_writer)

if(ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS)
  set_source_files_properties(
    src/compression/zstd.cpp
    PROPERTIES COMPILE_DEFINITIONS DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
  )
endif()

target_include_directories(dwarfs_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

target_compile_definitions(
  dwarfs_common PRIVATE
  DWARFS_SYSTEM_ID="${CMAKE_SYSTEM_NAME} [${CMAKE_SYSTEM_PROCESSOR}]"
  DWARFS_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

if(ENABLE_RICEPP)
  target_link_libraries(dwarfs_common PRIVATE ${RICEPP_OBJECT_TARGETS})
endif()

target_link_libraries(dwarfs_common PRIVATE dwarfs_thrift_lite_v2 dwarfs_frozen)

if(WIN32)
  target_link_libraries(dwarfs_common PRIVATE bcrypt.lib)
endif()

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

if(ENABLE_STACKTRACE)
  target_link_libraries(dwarfs_common PUBLIC cpptrace::cpptrace)
endif()

target_link_libraries(dwarfs_extractor PRIVATE PkgConfig::LIBARCHIVE)

target_include_directories(dwarfs_common SYSTEM PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fsst>)

target_link_libraries(
  dwarfs_common
  PUBLIC
  Boost::boost
  Boost::chrono
  Boost::filesystem
  dwarfs_compression_thrift
  dwarfs_metadata_thrift
  dwarfs_history_thrift
  dwarfs_features_thrift
  dwarfs_fsst
)

target_link_libraries(
  dwarfs_writer
  PUBLIC
  Boost::program_options
)

if(TARGET Boost::process)
  target_link_libraries(dwarfs_common PUBLIC Boost::process)
endif()

list(APPEND LIBDWARFS_TARGETS
  dwarfs_common
  dwarfs_compressor
  dwarfs_decompressor
  dwarfs_reader
  dwarfs_writer
  dwarfs_extractor
  dwarfs_rewrite
)

list(APPEND LIBDWARFS_OBJECT_TARGETS
  dwarfs_frozen
  dwarfs_thrift_lite_v2
  dwarfs_compression_thrift
  dwarfs_metadata_thrift
  dwarfs_history_thrift
  dwarfs_features_thrift
  dwarfs_fsst
)

if(NOT STATIC_BUILD_DO_NOT_USE)
  foreach(tgt ${LIBDWARFS_TARGETS})
    set_target_properties(${tgt} PROPERTIES VERSION ${PRJ_VERSION_MAJOR}.${PRJ_VERSION_MINOR}.${PRJ_VERSION_PATCH})
  endforeach()

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
    TARGETS ${LIBDWARFS_TARGETS}

            # object libs
            ${LIBDWARFS_OBJECT_TARGETS}

            # other
            ${RICEPP_OBJECT_TARGETS}
    EXPORT dwarfs-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

  install(
    DIRECTORY include/dwarfs
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN include/dwarfs/internal EXCLUDE
    PATTERN include/dwarfs/*/internal EXCLUDE
  )

  if(DWARFS_GIT_BUILD)
    install(
      FILES ${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs/version.h
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dwarfs
    )
  endif()

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/include/dwarfs/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dwarfs
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/dwarfs-config.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/dwarfs-config-version.cmake
    DESTINATION ${DWARFS_CMAKE_INSTALL_DIR}
  )

  install(
    EXPORT dwarfs-targets
    FILE dwarfs-targets.cmake
    NAMESPACE dwarfs::
    DESTINATION ${DWARFS_CMAKE_INSTALL_DIR}
  )
endif()
