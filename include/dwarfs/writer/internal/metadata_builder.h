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

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace dwarfs {

struct filesystem_version;

class logger;

namespace writer {
struct metadata_options;
}

namespace thrift::metadata {
class fs_options;
class metadata;
} // namespace thrift::metadata

namespace writer::internal {

class global_entry_data;
class inode_manager;
class block_manager;
class dir;

struct block_chunk {
  size_t block{};
  size_t offset{};
  size_t size{};
};

struct block_mapping {
  size_t old_block{};
  std::vector<block_chunk> chunks{};

  std::vector<block_chunk> map_chunk(size_t offset, size_t size) const;
};

class metadata_builder {
 public:
  // Start with empty metadata
  metadata_builder(logger& lgr, metadata_options const& options);

  // Start with existing metadata, upgrade if necessary
  metadata_builder(logger& lgr, thrift::metadata::metadata const& md,
                   thrift::metadata::fs_options const* orig_fs_options,
                   filesystem_version const& orig_fs_version,
                   metadata_options const& options);
  metadata_builder(logger& lgr, thrift::metadata::metadata&& md,
                   thrift::metadata::fs_options const* orig_fs_options,
                   filesystem_version const& orig_fs_version,
                   metadata_options const& options);

  ~metadata_builder();

  void set_devices(std::vector<uint64_t> devices) {
    impl_->set_devices(std::move(devices));
  }

  void set_symlink_table_size(size_t size) {
    impl_->set_symlink_table_size(size);
  }

  void set_block_size(uint32_t block_size) {
    impl_->set_block_size(block_size);
  }

#if 0
  void
  set_total_fs_size(uint64_t total_fs_size, uint64_t total_allocated_fs_size) {
    impl_->set_total_fs_size(total_fs_size, total_allocated_fs_size);
  }

  void set_total_hardlink_size(uint64_t total_hardlink_size) {
    impl_->set_total_hardlink_size(total_hardlink_size);
  }
#endif

  void set_shared_files_table(std::vector<uint32_t> shared_files) {
    impl_->set_shared_files_table(std::move(shared_files));
  }

  void set_category_names(std::vector<std::string> category_names) {
    impl_->set_category_names(std::move(category_names));
  }

  void set_block_categories(std::vector<uint32_t> block_categories) {
    impl_->set_block_categories(std::move(block_categories));
  }

  void set_category_metadata_json(std::vector<std::string> metadata_json) {
    impl_->set_category_metadata_json(std::move(metadata_json));
  }

  void
  set_block_category_metadata(std::map<uint32_t, uint32_t> block_metadata) {
    impl_->set_block_category_metadata(std::move(block_metadata));
  }

  void add_symlink_table_entry(size_t index, uint32_t entry) {
    impl_->add_symlink_table_entry(index, entry);
  }

  void gather_chunks(inode_manager const& im, block_manager const& bm,
                     size_t chunk_count) {
    impl_->gather_chunks(im, bm, chunk_count);
  }

  void gather_entries(std::span<dir*> dirs, global_entry_data const& ge_data,
                      uint32_t num_inodes) {
    impl_->gather_entries(dirs, ge_data, num_inodes);
  }

  void gather_global_entry_data(global_entry_data const& ge_data) {
    impl_->gather_global_entry_data(ge_data);
  }

  void
  remap_blocks(std::span<block_mapping const> mapping, size_t new_block_count) {
    impl_->remap_blocks(mapping, new_block_count);
  }

  thrift::metadata::metadata const& build() { return impl_->build(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_devices(std::vector<uint64_t> devices) = 0;
    virtual void set_symlink_table_size(size_t size) = 0;
    virtual void set_block_size(uint32_t block_size) = 0;
#if 0
    virtual void set_total_fs_size(uint64_t total_fs_size,
                                   uint64_t total_allocated_fs_size) = 0;
    virtual void set_total_hardlink_size(uint64_t total_hardlink_size) = 0;
#endif
    virtual void set_shared_files_table(std::vector<uint32_t> shared_files) = 0;
    virtual void
    set_category_names(std::vector<std::string> category_names) = 0;
    virtual void
    set_block_categories(std::vector<uint32_t> block_categories) = 0;
    virtual void
    set_category_metadata_json(std::vector<std::string> metadata_json) = 0;
    virtual void set_block_category_metadata(
        std::map<uint32_t, uint32_t> block_metadata) = 0;
    virtual void add_symlink_table_entry(size_t index, uint32_t entry) = 0;
    virtual void gather_chunks(inode_manager const& im, block_manager const& bm,
                               size_t chunk_count) = 0;
    virtual void
    gather_entries(std::span<dir*> dirs, global_entry_data const& ge_data,
                   uint32_t num_inodes) = 0;
    virtual void gather_global_entry_data(global_entry_data const& ge_data) = 0;
    virtual void remap_blocks(std::span<block_mapping const> mapping,
                              size_t new_block_count) = 0;

    virtual thrift::metadata::metadata const& build() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer::internal

} // namespace dwarfs
