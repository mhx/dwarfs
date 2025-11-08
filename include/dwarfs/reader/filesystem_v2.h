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

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <dwarfs/file_extents_iterable.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/reader/block_range.h>
#include <dwarfs/reader/fsinfo_features.h>
#include <dwarfs/reader/metadata_types.h>
#include <dwarfs/types.h>

namespace dwarfs {

struct vfs_stat;

class history;
class logger;
class os_access;
class performance_monitor;

namespace thrift::metadata {
class fs_options;
class metadata;
} // namespace thrift::metadata

namespace reader {

struct cache_tidy_config;
struct filesystem_options;
struct fsinfo_options;
struct getattr_options;
struct iovec_read_buf;

enum class filesystem_check_level { CHECKSUM, INTEGRITY, FULL };

namespace internal {

class filesystem_parser;

} // namespace internal

class filesystem_v2_lite {
 public:
  filesystem_v2_lite() = default;

  filesystem_v2_lite(logger& lgr, os_access const& os,
                     std::filesystem::path const& path);

  filesystem_v2_lite(
      logger& lgr, os_access const& os, std::filesystem::path const& path,
      filesystem_options const& options,
      std::shared_ptr<performance_monitor const> const& perfmon = nullptr);

  filesystem_v2_lite(logger& lgr, os_access const& os, file_view const& mm);

  filesystem_v2_lite(
      logger& lgr, os_access const& os, file_view const& mm,
      filesystem_options const& options,
      std::shared_ptr<performance_monitor const> const& perfmon = nullptr);

  filesystem_version version() const { return lite_->version(); }

  bool has_valid_section_index() const {
    return lite_->has_valid_section_index();
  }

  void walk(std::function<void(dir_entry_view)> const& func) const {
    lite_->walk(func);
  }

  void walk_data_order(std::function<void(dir_entry_view)> const& func) const {
    lite_->walk_data_order(func);
  }

  void walk_directories(std::function<void(dir_entry_view)> const& func) const {
    lite_->walk_directories(func);
  }

  dir_entry_view root() const { return lite_->root(); }

  std::optional<dir_entry_view> find(std::string_view path) const {
    return lite_->find(path);
  }

  std::optional<inode_view> find(int inode) const { return lite_->find(inode); }

  std::optional<dir_entry_view> find(int inode, std::string_view name) const {
    return lite_->find(inode, name);
  }

  file_stat getattr(inode_view entry, std::error_code& ec) const {
    return lite_->getattr(std::move(entry), ec);
  }

  file_stat getattr(inode_view entry) const {
    return lite_->getattr(std::move(entry));
  }

  file_stat getattr(inode_view entry, getattr_options const& opts,
                    std::error_code& ec) const {
    return lite_->getattr(std::move(entry), opts, ec);
  }

  file_stat getattr(inode_view entry, getattr_options const& opts) const {
    return lite_->getattr(std::move(entry), opts);
  }

  bool access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid) const {
    return lite_->access(std::move(entry), mode, uid, gid);
  }

  void access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const {
    lite_->access(std::move(entry), mode, uid, gid, ec);
  }

  std::optional<directory_view> opendir(inode_view entry) const {
    return lite_->opendir(std::move(entry));
  }

  std::optional<dir_entry_view>
  readdir(directory_view dir, size_t offset) const {
    return lite_->readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const { return lite_->dirsize(dir); }

  std::string
  readlink(inode_view entry, readlink_mode mode, std::error_code& ec) const {
    return lite_->readlink(std::move(entry), mode, ec);
  }

  std::string readlink(inode_view entry, std::error_code& ec) const {
    return lite_->readlink(std::move(entry), readlink_mode::preferred, ec);
  }

  std::string readlink(inode_view entry,
                       readlink_mode mode = readlink_mode::preferred) const {
    return lite_->readlink(std::move(entry), mode);
  }

  void statvfs(vfs_stat* stbuf) const { lite_->statvfs(stbuf); }

  int open(inode_view entry) const { return lite_->open(std::move(entry)); }

  int open(inode_view entry, std::error_code& ec) const {
    return lite_->open(std::move(entry), ec);
  }

  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence) const {
    return lite_->seek(inode, offset, whence);
  }

  file_off_t seek(uint32_t inode, file_off_t offset, seek_whence whence,
                  std::error_code& ec) const {
    return lite_->seek(inode, offset, whence, ec);
  }

  std::string read_string(uint32_t inode) const {
    return lite_->read_string(inode);
  }

  std::string read_string(uint32_t inode, std::error_code& ec) const {
    return lite_->read_string(inode, ec);
  }

  std::string
  read_string(uint32_t inode, size_t size, file_off_t offset = 0) const {
    return lite_->read_string(inode, size, offset);
  }

  std::string
  read_string(uint32_t inode, size_t size, std::error_code& ec) const {
    return lite_->read_string(inode, size, 0, ec);
  }

  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          std::error_code& ec) const {
    return lite_->read_string(inode, size, offset, ec);
  }

  size_t
  read(uint32_t inode, char* buf, size_t size, file_off_t offset = 0) const {
    return lite_->read(inode, buf, size, offset);
  }

  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const {
    return lite_->read(inode, buf, size, offset, ec);
  }

  size_t
  read(uint32_t inode, char* buf, size_t size, std::error_code& ec) const {
    return lite_->read(inode, buf, size, 0, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf) const {
    return lite_->readv(inode, buf);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, std::error_code& ec) const {
    return lite_->readv(inode, buf, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const {
    return lite_->readv(inode, buf, size, offset, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov, std::error_code& ec) const {
    return lite_->readv(inode, buf, size, offset, maxiov, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               std::error_code& ec) const {
    return lite_->readv(inode, buf, size, 0, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset = 0) const {
    return lite_->readv(inode, buf, size, offset);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov) const {
    return lite_->readv(inode, buf, size, offset, maxiov);
  }

  std::vector<std::future<block_range>> readv(uint32_t inode) const {
    return lite_->readv(inode);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, std::error_code& ec) const {
    return lite_->readv(inode, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset = 0) const {
    return lite_->readv(inode, size, offset);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov) const {
    return lite_->readv(inode, size, offset, maxiov);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, std::error_code& ec) const {
    return lite_->readv(inode, size, 0, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        std::error_code& ec) const {
    return lite_->readv(inode, size, offset, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        std::error_code& ec) const {
    return lite_->readv(inode, size, offset, maxiov, ec);
  }

  void set_num_workers(size_t num) { lite_->set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) {
    lite_->set_cache_tidy_config(cfg);
  }

  size_t num_blocks() const { return lite_->num_blocks(); }

  bool has_symlinks() const { return lite_->has_symlinks(); }

  bool has_sparse_files() const { return lite_->has_sparse_files(); }

  nlohmann::json get_inode_info(inode_view entry) const {
    return lite_->get_inode_info(std::move(entry));
  }

  nlohmann::json get_inode_info(inode_view entry, size_t max_chunks) const {
    return lite_->get_inode_info(std::move(entry), max_chunks);
  }

  std::vector<std::string> get_all_block_categories() const {
    return lite_->get_all_block_categories();
  }

  std::vector<file_stat::uid_type> get_all_uids() const {
    return lite_->get_all_uids();
  }

  std::vector<file_stat::gid_type> get_all_gids() const {
    return lite_->get_all_gids();
  }

  std::optional<std::string> get_block_category(size_t block_number) const {
    return lite_->get_block_category(block_number);
  }

  std::optional<nlohmann::json>
  get_block_category_metadata(size_t block_number) const {
    return lite_->get_block_category_metadata(block_number);
  }

  void cache_blocks_by_category(std::string_view category) const {
    lite_->cache_blocks_by_category(category);
  }

  void cache_all_blocks() const { lite_->cache_all_blocks(); }

  std::shared_ptr<internal::filesystem_parser> get_parser() const {
    return lite_->get_parser();
  }

  class impl_lite {
   public:
    virtual ~impl_lite() = default;

    virtual filesystem_version version() const = 0;
    virtual bool has_valid_section_index() const = 0;
    virtual void
    walk(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual void
    walk_data_order(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual void
    walk_directories(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual dir_entry_view root() const = 0;
    virtual std::optional<dir_entry_view> find(std::string_view path) const = 0;
    virtual std::optional<inode_view> find(int inode) const = 0;
    virtual std::optional<dir_entry_view>
    find(int inode, std::string_view name) const = 0;
    virtual file_stat getattr(inode_view entry, std::error_code& ec) const = 0;
    virtual file_stat getattr(inode_view entry, getattr_options const& opts,
                              std::error_code& ec) const = 0;
    virtual file_stat getattr(inode_view entry) const = 0;
    virtual file_stat
    getattr(inode_view entry, getattr_options const& opts) const = 0;
    virtual bool access(inode_view entry, int mode, file_stat::uid_type uid,
                        file_stat::gid_type gid) const = 0;
    virtual void access(inode_view entry, int mode, file_stat::uid_type uid,
                        file_stat::gid_type gid, std::error_code& ec) const = 0;
    virtual std::optional<directory_view> opendir(inode_view entry) const = 0;
    virtual std::optional<dir_entry_view>
    readdir(directory_view dir, size_t offset) const = 0;
    virtual size_t dirsize(directory_view dir) const = 0;
    virtual std::string readlink(inode_view entry, readlink_mode mode,
                                 std::error_code& ec) const = 0;
    virtual std::string
    readlink(inode_view entry, readlink_mode mode) const = 0;
    virtual void statvfs(vfs_stat* stbuf) const = 0;
    virtual int open(inode_view entry) const = 0;
    virtual int open(inode_view entry, std::error_code& ec) const = 0;
    virtual file_off_t
    seek(uint32_t inode, file_off_t offset, seek_whence whence) const = 0;
    virtual file_off_t seek(uint32_t inode, file_off_t offset,
                            seek_whence whence, std::error_code& ec) const = 0;
    virtual std::string read_string(uint32_t inode) const = 0;
    virtual std::string
    read_string(uint32_t inode, std::error_code& ec) const = 0;
    virtual std::string
    read_string(uint32_t inode, size_t size, file_off_t offset) const = 0;
    virtual std::string
    read_string(uint32_t inode, size_t size, file_off_t offset,
                std::error_code& ec) const = 0;
    virtual size_t
    read(uint32_t inode, char* buf, size_t size, file_off_t offset) const = 0;
    virtual size_t read(uint32_t inode, char* buf, size_t size,
                        file_off_t offset, std::error_code& ec) const = 0;
    virtual size_t readv(uint32_t inode, iovec_read_buf& buf) const = 0;
    virtual size_t
    readv(uint32_t inode, iovec_read_buf& buf, std::error_code& ec) const = 0;
    virtual size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                         file_off_t offset, std::error_code& ec) const = 0;
    virtual size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                         file_off_t offset) const = 0;
    virtual size_t
    readv(uint32_t inode, iovec_read_buf& buf, size_t size, file_off_t offset,
          size_t maxiov, std::error_code& ec) const = 0;
    virtual size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                         file_off_t offset, size_t maxiov) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, std::error_code& ec) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, size_t size, file_off_t offset) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, size_t size, file_off_t offset,
          std::error_code& ec) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, size_t size, file_off_t offset,
          size_t maxiov) const = 0;
    virtual std::vector<std::future<block_range>>
    readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
          std::error_code& ec) const = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_cache_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual size_t num_blocks() const = 0;
    virtual bool has_symlinks() const = 0;
    virtual bool has_sparse_files() const = 0;
    virtual nlohmann::json get_inode_info(inode_view entry) const = 0;
    virtual nlohmann::json
    get_inode_info(inode_view entry, size_t max_chunks) const = 0;
    virtual std::vector<std::string> get_all_block_categories() const = 0;
    virtual std::vector<file_stat::uid_type> get_all_uids() const = 0;
    virtual std::vector<file_stat::gid_type> get_all_gids() const = 0;
    virtual std::optional<std::string>
    get_block_category(size_t block_number) const = 0;
    virtual std::optional<nlohmann::json>
    get_block_category_metadata(size_t block_number) const = 0;
    virtual void cache_blocks_by_category(std::string_view category) const = 0;
    virtual void cache_all_blocks() const = 0;
    virtual std::shared_ptr<internal::filesystem_parser> get_parser() const = 0;
  };

 protected:
  explicit filesystem_v2_lite(std::unique_ptr<impl_lite> impl)
      : lite_{std::move(impl)} {}

  template <typename T>
    requires std::derived_from<T, impl_lite>
  T const& as_() const {
    return dynamic_cast<T const&>(*lite_);
  }

 private:
  std::unique_ptr<impl_lite> lite_;
};

class filesystem_v2 final : public filesystem_v2_lite {
 public:
  filesystem_v2() = default;

  filesystem_v2(logger& lgr, os_access const& os,
                std::filesystem::path const& path);

  filesystem_v2(
      logger& lgr, os_access const& os, std::filesystem::path const& path,
      filesystem_options const& options,
      std::shared_ptr<performance_monitor const> const& perfmon = nullptr);

  filesystem_v2(logger& lgr, os_access const& os, file_view const& mm);

  filesystem_v2(
      logger& lgr, os_access const& os, file_view const& mm,
      filesystem_options const& options,
      std::shared_ptr<performance_monitor const> const& perfmon = nullptr);

  static int
  identify(logger& lgr, os_access const& os, file_view const& mm,
           std::ostream& output, int detail_level = 0, size_t num_readers = 1,
           bool check_integrity = false, file_off_t image_offset = 0);

  static std::optional<file_extents_iterable>
  header(logger& lgr, file_view const& mm);

  static std::optional<file_extents_iterable>
  header(logger& lgr, file_view const& mm, file_off_t image_offset);

  int check(filesystem_check_level level, size_t num_threads = 0) const;

  void dump(std::ostream& os, fsinfo_options const& opts) const;
  std::string dump(fsinfo_options const& opts) const;

  nlohmann::json info_as_json(fsinfo_options const& opts) const;
  nlohmann::json metadata_as_json() const;
  std::string serialize_metadata_as_json(bool simple) const;

  std::optional<file_extents_iterable> header() const;

  history const& get_history() const;

  std::unique_ptr<thrift::metadata::metadata> thawed_metadata() const;
  std::unique_ptr<thrift::metadata::metadata> unpacked_metadata() const;

  std::unique_ptr<thrift::metadata::fs_options> thawed_fs_options() const;

  std::future<block_range>
  read_raw_block_data(size_t block_no, size_t offset, size_t size) const;

  class impl : public impl_lite {
   public:
    virtual int
    check(filesystem_check_level level, size_t num_threads) const = 0;
    virtual void dump(std::ostream& os, fsinfo_options const& opts) const = 0;
    virtual std::string dump(fsinfo_options const& opts) const = 0;
    virtual nlohmann::json info_as_json(fsinfo_options const& opts) const = 0;
    virtual nlohmann::json metadata_as_json() const = 0;
    virtual std::string serialize_metadata_as_json(bool simple) const = 0;
    virtual std::optional<file_extents_iterable> header() const = 0;
    virtual history const& get_history() const = 0;
    virtual std::unique_ptr<thrift::metadata::metadata>
    thawed_metadata() const = 0;
    virtual std::unique_ptr<thrift::metadata::metadata>
    unpacked_metadata() const = 0;
    virtual std::unique_ptr<thrift::metadata::fs_options>
    thawed_fs_options() const = 0;
    virtual std::future<block_range>
    read_raw_block_data(size_t block, size_t offset, size_t size) const = 0;
  };

 private:
  impl const& full_() const;
};

} // namespace reader
} // namespace dwarfs
