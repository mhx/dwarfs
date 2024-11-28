/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <ctime>
#include <filesystem>

#include <dwarfs/logger.h>
#include <dwarfs/version.h>

#include <dwarfs/writer/metadata_options.h>

#include <dwarfs/internal/features.h>
#include <dwarfs/internal/string_table.h>
#include <dwarfs/writer/internal/block_manager.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/metadata_builder.h>

#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::writer::internal {

namespace {

using namespace dwarfs::internal;

template <typename LoggerPolicy>
class metadata_builder_ final : public metadata_builder::impl {
 public:
  metadata_builder_(logger& lgr, metadata_options const& options)
      : LOG_PROXY_INIT(lgr)
      , options_{options} {}

  metadata_builder_(logger& lgr, thrift::metadata::metadata const& md,
                    metadata_options const& options)
      : LOG_PROXY_INIT(lgr)
      , md_{md}
      , options_{options} {}

  metadata_builder_(logger& lgr, thrift::metadata::metadata&& md,
                    metadata_options const& options)
      : LOG_PROXY_INIT(lgr)
      , md_{std::move(md)}
      , options_{options} {}

  void set_devices(std::vector<uint64_t> devices) override {
    md_.devices() = std::move(devices);
  }

  void set_symlink_table_size(size_t size) override {
    md_.symlink_table()->resize(size);
  }

  void set_block_size(uint32_t block_size) override {
    md_.block_size() = block_size;
  }

  void set_total_fs_size(uint64_t total_fs_size) override {
    md_.total_fs_size() = total_fs_size;
  }

  void set_total_hardlink_size(uint64_t total_hardlink_size) override {
    md_.total_hardlink_size() = total_hardlink_size;
  }

  void set_shared_files_table(std::vector<uint32_t> shared_files) override {
    md_.shared_files_table() = std::move(shared_files);
  }

  void set_category_names(std::vector<std::string> category_names) override {
    md_.category_names() = std::move(category_names);
  }

  void set_block_categories(std::vector<uint32_t> block_categories) override {
    md_.block_categories() = std::move(block_categories);
  }

  void
  set_category_metadata_json(std::vector<std::string> metadata_json) override {
    md_.category_metadata_json() = std::move(metadata_json);
  }

  void set_block_category_metadata(
      std::map<uint32_t, uint32_t> block_metadata) override {
    md_.block_category_metadata() = std::move(block_metadata);
  }

  void add_symlink_table_entry(size_t index, uint32_t entry) override {
    DWARFS_NOTHROW(md_.symlink_table()->at(index)) = entry;
  }

  void gather_chunks(inode_manager const& im, block_manager const& bm,
                     size_t chunk_count) override;

  void gather_entries(std::span<dir*> dirs, global_entry_data const& ge_data,
                      uint32_t num_inodes) override;

  void gather_global_entry_data(global_entry_data const& ge_data) override;

  thrift::metadata::metadata const& build() override;

 private:
  thrift::metadata::inode_size_cache build_inode_size_cache() const;

  LOG_PROXY_DECL(LoggerPolicy);
  thrift::metadata::metadata md_;
  feature_set features_;
  metadata_options const& options_;
};

template <typename LoggerPolicy>
thrift::metadata::inode_size_cache
metadata_builder_<LoggerPolicy>::build_inode_size_cache() const {
  auto tv = LOG_TIMED_VERBOSE;

  thrift::metadata::inode_size_cache cache;
  cache.min_chunk_count() = options_.inode_size_cache_min_chunk_count;

  auto const& chunk_table = md_.chunk_table().value();
  auto const& chunks = md_.chunks().value();

  for (size_t ino = 1; ino < chunk_table.size() - 1; ++ino) {
    auto const begin = chunk_table[ino];
    auto const end = chunk_table[ino + 1];
    auto const num_chunks = end - begin;

    if (num_chunks >= options_.inode_size_cache_min_chunk_count) {
      uint64_t size = 0;

      for (uint32_t ix = begin; ix < end; ++ix) {
        auto const& chunk = chunks[ix];
        size += chunk.size().value();
      }

      LOG_DEBUG << "caching size " << size << " for inode " << ino << " with "
                << num_chunks << " chunks";

      cache.lookup()->emplace(ino, size);
    }
  }

  tv << "building inode size cache...";

  return cache;
}

template <typename LoggerPolicy>
void metadata_builder_<LoggerPolicy>::gather_chunks(inode_manager const& im,
                                                    block_manager const& bm,
                                                    size_t chunk_count) {
  md_.chunk_table()->resize(im.count() + 1);
  md_.chunks().value().reserve(chunk_count);

  im.for_each_inode_in_order([&](std::shared_ptr<inode> const& ino) {
    auto const total_chunks = md_.chunks()->size();
    DWARFS_NOTHROW(md_.chunk_table()->at(ino->num())) = total_chunks;
    if (!ino->append_chunks_to(md_.chunks().value())) {
      std::ostringstream oss;
      for (auto fp : ino->all()) {
        oss << "\n  " << fp->path_as_string();
      }
      LOG_ERROR << "inconsistent fragments in inode " << ino->num()
                << ", the following files will be empty:" << oss.str();
    }
  });

  bm.map_logical_blocks(md_.chunks().value());

  // insert dummy inode to help determine number of chunks per inode
  DWARFS_NOTHROW(md_.chunk_table()->at(im.count())) = md_.chunks()->size();

  LOG_DEBUG << "total number of unique files: " << im.count();
  LOG_DEBUG << "total number of chunks: " << md_.chunks()->size();
}

template <typename LoggerPolicy>
void metadata_builder_<LoggerPolicy>::gather_entries(
    std::span<dir*> dirs, global_entry_data const& ge_data,
    uint32_t num_inodes) {
  md_.dir_entries() = std::vector<thrift::metadata::dir_entry>();
  md_.inodes()->resize(num_inodes);
  md_.directories()->reserve(dirs.size() + 1);

  for (auto p : dirs) {
    if (!p->has_parent()) {
      p->set_entry_index(md_.dir_entries()->size());
      p->pack_entry(md_, ge_data);
    }

    p->pack(md_, ge_data);
  }

  thrift::metadata::directory dummy;
  dummy.parent_entry() = 0;
  dummy.first_entry() = md_.dir_entries()->size();
  dummy.self_entry() = 0;
  md_.directories()->push_back(dummy);
}

template <typename LoggerPolicy>
void metadata_builder_<LoggerPolicy>::gather_global_entry_data(
    global_entry_data const& ge_data) {
  md_.names() = ge_data.get_names();

  md_.symlinks() = ge_data.get_symlinks();

  md_.uids() = options_.uid ? std::vector<file_stat::uid_type>{*options_.uid}
                            : ge_data.get_uids();

  md_.gids() = options_.gid ? std::vector<file_stat::gid_type>{*options_.gid}
                            : ge_data.get_gids();

  md_.modes() = ge_data.get_modes();

  md_.timestamp_base() = ge_data.get_timestamp_base();
}

template <typename LoggerPolicy>
thrift::metadata::metadata const& metadata_builder_<LoggerPolicy>::build() {
  LOG_VERBOSE << "building metadata";

  thrift::metadata::fs_options fsopts;
  fsopts.mtime_only() = !options_.keep_all_times;
  if (options_.time_resolution_sec > 1) {
    fsopts.time_resolution_sec() = options_.time_resolution_sec;
  }
  fsopts.packed_chunk_table() = options_.pack_chunk_table;
  fsopts.packed_directories() = options_.pack_directories;
  fsopts.packed_shared_files_table() = options_.pack_shared_files_table;

  if (options_.pack_directories) {
    // pack directories
    uint32_t last_first_entry = 0;

    for (auto& d : md_.directories().value()) {
      d.parent_entry() = 0; // this will be recovered
      d.self_entry() = 0;   // this will be recovered
      auto delta = d.first_entry().value() - last_first_entry;
      last_first_entry = d.first_entry().value();
      d.first_entry() = delta;
    }
  }

  md_.reg_file_size_cache() = build_inode_size_cache();

  if (options_.pack_chunk_table) {
    // delta-compress chunk table
    std::adjacent_difference(md_.chunk_table()->begin(),
                             md_.chunk_table()->end(),
                             md_.chunk_table()->begin());
  }

  if (options_.pack_shared_files_table) {
    if (!md_.shared_files_table()->empty()) {
      auto& sf = md_.shared_files_table().value();
      DWARFS_CHECK(std::ranges::is_sorted(sf),
                   "shared files vector not sorted");
      std::vector<uint32_t> compressed;
      compressed.reserve(sf.back() + 1);

      uint32_t count = 0;
      uint32_t index = 0;
      for (auto i : sf) {
        if (i == index) {
          ++count;
        } else {
          ++index;
          DWARFS_CHECK(i == index, "inconsistent shared files vector");
          DWARFS_CHECK(count >= 2, "unique file in shared files vector");
          compressed.emplace_back(count - 2);
          count = 1;
        }
      }

      compressed.emplace_back(count - 2);

      DWARFS_CHECK(compressed.size() == sf.back() + 1,
                   "unexpected compressed vector size");

      sf.swap(compressed);
    }
  }

  if (!options_.plain_names_table) {
    auto ti = LOG_TIMED_INFO;
    md_.compact_names() = string_table::pack(
        md_.names().value(), string_table::pack_options(
                                 options_.pack_names, options_.pack_names_index,
                                 options_.force_pack_string_tables));
    thrift::metadata::metadata tmp;
    md_.names().copy_from(tmp.names());
    ti << "saving names table...";
  }

  if (!options_.plain_symlinks_table) {
    auto ti = LOG_TIMED_INFO;
    md_.compact_symlinks() = string_table::pack(
        md_.symlinks().value(),
        string_table::pack_options(options_.pack_symlinks,
                                   options_.pack_symlinks_index,
                                   options_.force_pack_string_tables));
    thrift::metadata::metadata tmp;
    md_.symlinks().copy_from(tmp.symlinks());
    ti << "saving symlinks table...";
  }

  if (options_.no_category_names) {
    md_.category_names().reset();
    md_.block_categories().reset();
  }

  if (options_.no_category_names || options_.no_category_metadata) {
    md_.category_metadata_json().reset();
    md_.block_category_metadata().reset();
  }

  // TODO: don't overwrite all options when upgrading!
  md_.options() = fsopts;
  md_.features() = features_.get();
  md_.dwarfs_version() = std::string("libdwarfs ") + DWARFS_GIT_ID;
  if (!options_.no_create_timestamp) {
    md_.create_timestamp() = std::time(nullptr);
  }
  md_.preferred_path_separator() =
      static_cast<uint32_t>(std::filesystem::path::preferred_separator);

  return md_;
}

} // namespace

metadata_builder::metadata_builder(logger& lgr, metadata_options const& options)
    : impl_{
          make_unique_logging_object<impl, metadata_builder_, logger_policies>(
              lgr, options)} {}

metadata_builder::metadata_builder(logger& lgr,
                                   thrift::metadata::metadata const& md,
                                   metadata_options const& options)
    : impl_{
          make_unique_logging_object<impl, metadata_builder_, logger_policies>(
              lgr, md, options)} {}

metadata_builder::metadata_builder(logger& lgr, thrift::metadata::metadata&& md,
                                   metadata_options const& options)
    : impl_{
          make_unique_logging_object<impl, metadata_builder_, logger_policies>(
              lgr, std::move(md), options)} {}

metadata_builder::~metadata_builder() = default;

} // namespace dwarfs::writer::internal
