/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <dwarfs/reader/metadata_types.h>

#include <dwarfs/reader/internal/metadata_types.h>

namespace dwarfs {

class logger;

struct filesystem_info;
struct vfs_stat;

class performance_monitor;

namespace thrift::metadata {
class fs_options;
class metadata;
} // namespace thrift::metadata

namespace reader {

struct fsinfo_options;
struct getattr_options;
struct metadata_options;

namespace internal {

class metadata_v2_data;

class metadata_v2 {
 public:
  metadata_v2() = default;
  metadata_v2(metadata_v2&&) = default;
  metadata_v2& operator=(metadata_v2&&) = default;

  metadata_v2(
      logger& lgr, std::span<uint8_t const> schema,
      std::span<uint8_t const> data, metadata_options const& options,
      int inode_offset = 0, bool force_consistency_check = false,
      std::shared_ptr<performance_monitor const> const& perfmon = nullptr);

  void check_consistency() const { impl_->check_consistency(); }

  size_t size() const { return impl_->size(); }

  void walk(std::function<void(dir_entry_view)> const& func) const {
    impl_->walk(func);
  }

  void walk_data_order(std::function<void(dir_entry_view)> const& func) const {
    impl_->walk_data_order(func);
  }

  dir_entry_view root() const { return impl_->root(); }

  std::optional<dir_entry_view> find(std::string_view path) const {
    return impl_->find(path);
  }

  std::optional<inode_view> find(int inode) const { return impl_->find(inode); }

  std::optional<dir_entry_view> find(int inode, std::string_view name) const {
    return impl_->find(inode, name);
  }

  file_stat getattr(inode_view iv, std::error_code& ec) const {
    return impl_->getattr(std::move(iv), ec);
  }

  file_stat getattr(inode_view iv, getattr_options const& opts,
                    std::error_code& ec) const {
    return impl_->getattr(std::move(iv), opts, ec);
  }

  std::optional<directory_view> opendir(inode_view iv) const {
    return impl_->opendir(std::move(iv));
  }

  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const {
    return impl_->readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const { return impl_->dirsize(dir); }

  void access(inode_view iv, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const {
    impl_->access(std::move(iv), mode, uid, gid, ec);
  }

  int open(inode_view iv, std::error_code& ec) const {
    return impl_->open(std::move(iv), ec);
  }

  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence,
                  std::error_code& ec) const {
    return impl_->seek(inode, offset, whence, ec);
  }

  std::string
  readlink(inode_view iv, readlink_mode mode, std::error_code& ec) const {
    return impl_->readlink(std::move(iv), mode, ec);
  }

  void statvfs(vfs_stat* stbuf) const { impl_->statvfs(stbuf); }

  chunk_range get_chunks(int inode, std::error_code& ec) const {
    return impl_->get_chunks(inode, ec);
  }

  size_t block_size() const { return impl_->block_size(); }

  bool has_symlinks() const { return impl_->has_symlinks(); }

  nlohmann::json get_inode_info(inode_view iv, size_t max_chunks) const {
    return impl_->get_inode_info(std::move(iv), max_chunks);
  }

  std::optional<std::string> get_block_category(size_t block_number) const {
    return impl_->get_block_category(block_number);
  }

  std::optional<nlohmann::json>
  get_block_category_metadata(size_t block_number) const {
    return impl_->get_block_category_metadata(block_number);
  }

  std::vector<std::string> get_all_block_categories() const {
    return impl_->get_all_block_categories();
  }

  std::vector<file_stat::uid_type> get_all_uids() const {
    return impl_->get_all_uids();
  }

  std::vector<file_stat::gid_type> get_all_gids() const {
    return impl_->get_all_gids();
  }

  std::vector<size_t>
  get_block_numbers_by_category(std::string_view category) const {
    return impl_->get_block_numbers_by_category(category);
  }

  metadata_v2_data const& internal_data() const {
    return impl_->internal_data();
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void check_consistency() const = 0;

    virtual size_t size() const = 0;

    virtual void
    walk(std::function<void(dir_entry_view)> const& func) const = 0;

    virtual void
    walk_data_order(std::function<void(dir_entry_view)> const& func) const = 0;

    virtual dir_entry_view root() const = 0;

    virtual std::optional<dir_entry_view> find(std::string_view path) const = 0;
    virtual std::optional<inode_view> find(int inode) const = 0;
    virtual std::optional<dir_entry_view>
    find(int inode, std::string_view name) const = 0;

    virtual file_stat getattr(inode_view iv, std::error_code& ec) const = 0;
    virtual file_stat getattr(inode_view iv, getattr_options const& opts,
                              std::error_code& ec) const = 0;

    virtual std::optional<directory_view> opendir(inode_view iv) const = 0;

    virtual std::optional<dir_entry_view>
    readdir(directory_view dir, size_t offset) const = 0;

    virtual size_t dirsize(directory_view dir) const = 0;

    virtual void access(inode_view iv, int mode, file_stat::uid_type uid,
                        file_stat::gid_type gid, std::error_code& ec) const = 0;

    virtual int open(inode_view iv, std::error_code& ec) const = 0;

    virtual file_off_t seek(uint32_t inode, file_off_t offset,
                            seek_whence whence, std::error_code& ec) const = 0;

    virtual std::string
    readlink(inode_view iv, readlink_mode mode, std::error_code& ec) const = 0;

    virtual void statvfs(vfs_stat* stbuf) const = 0;

    virtual chunk_range get_chunks(int inode, std::error_code& ec) const = 0;

    virtual size_t block_size() const = 0;

    virtual bool has_symlinks() const = 0;

    virtual nlohmann::json
    get_inode_info(inode_view iv, size_t max_chunks) const = 0;

    virtual std::optional<std::string>
    get_block_category(size_t block_number) const = 0;

    virtual std::optional<nlohmann::json>
    get_block_category_metadata(size_t block_number) const = 0;

    virtual std::vector<std::string> get_all_block_categories() const = 0;

    virtual std::vector<file_stat::uid_type> get_all_uids() const = 0;

    virtual std::vector<file_stat::gid_type> get_all_gids() const = 0;

    virtual std::vector<size_t>
    get_block_numbers_by_category(std::string_view category) const = 0;

    virtual metadata_v2_data const& internal_data() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class metadata_v2_utils {
 public:
  metadata_v2_utils(metadata_v2 const& meta);

  void dump(std::ostream& os, fsinfo_options const& opts,
            filesystem_info const* fsinfo,
            std::function<void(std::string const&, uint32_t)> const& icb) const;

  nlohmann::json
  info_as_json(fsinfo_options const& opts, filesystem_info const* fsinfo) const;

  nlohmann::json as_json() const;

  std::string serialize_as_json(bool simple) const;

  std::unique_ptr<thrift::metadata::metadata> thaw() const;

  std::unique_ptr<thrift::metadata::metadata> unpack() const;

  std::unique_ptr<thrift::metadata::fs_options> thaw_fs_options() const;

 private:
  metadata_v2_data const& data_;
};

} // namespace internal
} // namespace reader
} // namespace dwarfs
