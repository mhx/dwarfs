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
  src/internal/metadata_utils.cpp
  src/internal/mmap_file_view.cpp
  src/internal/option_parser.cpp
  src/internal/os_access_generic_data.cpp
  src/internal/string_table.cpp
  src/internal/thread_util.cpp
  src/internal/unicode_case_folding.cpp
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

  # src/writer/categorizer/binary_categorizer.cpp
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

add_cpp2_thrift_library(thrift/metadata.thrift FROZEN
                        TARGET dwarfs_metadata_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/compression.thrift
                        TARGET dwarfs_compression_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/history.thrift
                        TARGET dwarfs_history_thrift OUTPUT_PATH dwarfs)
add_cpp2_thrift_library(thrift/features.thrift
                        TARGET dwarfs_features_thrift OUTPUT_PATH dwarfs)

target_link_libraries(dwarfs_common PRIVATE dwarfs_folly_lite PkgConfig::LIBCRYPTO PkgConfig::XXHASH PkgConfig::ZSTD)
target_link_libraries(dwarfs_compressor PRIVATE dwarfs_common)
target_link_libraries(dwarfs_decompressor PRIVATE dwarfs_common)
target_link_libraries(dwarfs_reader PUBLIC dwarfs_common dwarfs_decompressor)
target_link_libraries(dwarfs_writer PUBLIC dwarfs_common dwarfs_compressor dwarfs_decompressor)
target_link_libraries(dwarfs_writer PRIVATE PkgConfig::ZSTD)
target_link_libraries(dwarfs_extractor PUBLIC dwarfs_reader)
target_link_libraries(dwarfs_rewrite PUBLIC dwarfs_reader dwarfs_writer)

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

target_link_libraries(dwarfs_common PRIVATE dwarfs_thrift_lite)

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
set_property(TARGET dwarfs_fsst PROPERTY CXX_STANDARD ${DWARFS_CXX_STANDARD})
set_property(TARGET dwarfs_fsst PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET dwarfs_fsst PROPERTY CXX_EXTENSIONS OFF)

target_link_libraries(
  dwarfs_common
  PUBLIC
  Boost::boost
  Boost::chrono
  dwarfs_compression_thrift
  dwarfs_metadata_thrift
  dwarfs_history_thrift
  dwarfs_features_thrift
  dwarfs_fsst
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
  dwarfs_folly_lite
  dwarfs_thrift_lite
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
            folly_deps
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
