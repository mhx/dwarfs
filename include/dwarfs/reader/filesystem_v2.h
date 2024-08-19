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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <dwarfs/file_stat.h>
#include <dwarfs/reader/block_range.h>
#include <dwarfs/reader/fsinfo_features.h>
#include <dwarfs/reader/metadata_types.h>
#include <dwarfs/types.h>

namespace dwarfs {

struct vfs_stat;

class history;
class logger;
class mmif;
class os_access;
class performance_monitor;

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

class filesystem_v2 {
 public:
  filesystem_v2() = default;

  filesystem_v2(logger& lgr, os_access const& os,
                std::filesystem::path const& path);

  filesystem_v2(logger& lgr, os_access const& os,
                std::filesystem::path const& path,
                filesystem_options const& options,
                std::shared_ptr<performance_monitor const> perfmon = nullptr);

  filesystem_v2(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm);

  filesystem_v2(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
                filesystem_options const& options,
                std::shared_ptr<performance_monitor const> perfmon = nullptr);

  static int
  identify(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
           std::ostream& output, int detail_level = 0, size_t num_readers = 1,
           bool check_integrity = false, file_off_t image_offset = 0);

  static std::optional<std::span<uint8_t const>>
  header(std::shared_ptr<mmif> mm);

  static std::optional<std::span<uint8_t const>>
  header(std::shared_ptr<mmif> mm, file_off_t image_offset);

  static fsinfo_features features_for_level(int level);

  int check(filesystem_check_level level, size_t num_threads = 0) const {
    return impl_->check(level, num_threads);
  }

  void dump(std::ostream& os, fsinfo_options const& opts) const {
    impl_->dump(os, opts);
  }

  std::string dump(fsinfo_options const& opts) const {
    return impl_->dump(opts);
  }

  nlohmann::json info_as_json(fsinfo_options const& opts) const {
    return impl_->info_as_json(opts);
  }

  nlohmann::json metadata_as_json() const { return impl_->metadata_as_json(); }

  std::string serialize_metadata_as_json(bool simple) const {
    return impl_->serialize_metadata_as_json(simple);
  }

  void walk(std::function<void(dir_entry_view)> const& func) const {
    impl_->walk(func);
  }

  void walk_data_order(std::function<void(dir_entry_view)> const& func) const {
    impl_->walk_data_order(func);
  }

  std::optional<inode_view> find(const char* path) const {
    return impl_->find(path);
  }

  std::optional<inode_view> find(int inode) const { return impl_->find(inode); }

  std::optional<inode_view> find(int inode, const char* name) const {
    return impl_->find(inode, name);
  }

  file_stat getattr(inode_view entry, std::error_code& ec) const {
    return impl_->getattr(entry, ec);
  }

  file_stat getattr(inode_view entry) const { return impl_->getattr(entry); }

  file_stat getattr(inode_view entry, getattr_options const& opts,
                    std::error_code& ec) const {
    return impl_->getattr(entry, opts, ec);
  }

  file_stat getattr(inode_view entry, getattr_options const& opts) const {
    return impl_->getattr(entry, opts);
  }

  bool access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid) const {
    return impl_->access(entry, mode, uid, gid);
  }

  void access(inode_view entry, int mode, file_stat::uid_type uid,
              file_stat::gid_type gid, std::error_code& ec) const {
    impl_->access(entry, mode, uid, gid, ec);
  }

  std::optional<directory_view> opendir(inode_view entry) const {
    return impl_->opendir(entry);
  }

  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const {
    return impl_->readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const { return impl_->dirsize(dir); }

  std::string
  readlink(inode_view entry, readlink_mode mode, std::error_code& ec) const {
    return impl_->readlink(entry, mode, ec);
  }

  std::string readlink(inode_view entry, std::error_code& ec) const {
    return impl_->readlink(entry, readlink_mode::preferred, ec);
  }

  std::string readlink(inode_view entry,
                       readlink_mode mode = readlink_mode::preferred) const {
    return impl_->readlink(entry, mode);
  }

  void statvfs(vfs_stat* stbuf) const { impl_->statvfs(stbuf); }

  int open(inode_view entry) const { return impl_->open(entry); }

  int open(inode_view entry, std::error_code& ec) const {
    return impl_->open(entry, ec);
  }

  std::string read_string(uint32_t inode) const {
    return impl_->read_string(inode);
  }

  std::string read_string(uint32_t inode, std::error_code& ec) const {
    return impl_->read_string(inode, ec);
  }

  std::string
  read_string(uint32_t inode, size_t size, file_off_t offset = 0) const {
    return impl_->read_string(inode, size, offset);
  }

  std::string
  read_string(uint32_t inode, size_t size, std::error_code& ec) const {
    return impl_->read_string(inode, size, 0, ec);
  }

  std::string read_string(uint32_t inode, size_t size, file_off_t offset,
                          std::error_code& ec) const {
    return impl_->read_string(inode, size, offset, ec);
  }

  size_t
  read(uint32_t inode, char* buf, size_t size, file_off_t offset = 0) const {
    return impl_->read(inode, buf, size, offset);
  }

  size_t read(uint32_t inode, char* buf, size_t size, file_off_t offset,
              std::error_code& ec) const {
    return impl_->read(inode, buf, size, offset, ec);
  }

  size_t
  read(uint32_t inode, char* buf, size_t size, std::error_code& ec) const {
    return impl_->read(inode, buf, size, 0, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf) const {
    return impl_->readv(inode, buf);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, std::error_code& ec) const {
    return impl_->readv(inode, buf, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, std::error_code& ec) const {
    return impl_->readv(inode, buf, size, offset, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov, std::error_code& ec) const {
    return impl_->readv(inode, buf, size, offset, maxiov, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               std::error_code& ec) const {
    return impl_->readv(inode, buf, size, 0, ec);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset = 0) const {
    return impl_->readv(inode, buf, size, offset);
  }

  size_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
               file_off_t offset, size_t maxiov) const {
    return impl_->readv(inode, buf, size, offset, maxiov);
  }

  std::vector<std::future<block_range>> readv(uint32_t inode) const {
    return impl_->readv(inode);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, std::error_code& ec) const {
    return impl_->readv(inode, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset = 0) const {
    return impl_->readv(inode, size, offset);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov) const {
    return impl_->readv(inode, size, offset, maxiov);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, std::error_code& ec) const {
    return impl_->readv(inode, size, 0, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset,
        std::error_code& ec) const {
    return impl_->readv(inode, size, offset, ec);
  }

  std::vector<std::future<block_range>>
  readv(uint32_t inode, size_t size, file_off_t offset, size_t maxiov,
        std::error_code& ec) const {
    return impl_->readv(inode, size, offset, maxiov, ec);
  }

  std::optional<std::span<uint8_t const>> header() const {
    return impl_->header();
  }

  void set_num_workers(size_t num) { return impl_->set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) {
    return impl_->set_cache_tidy_config(cfg);
  }

  size_t num_blocks() const { return impl_->num_blocks(); }

  bool has_symlinks() const { return impl_->has_symlinks(); }

  history const& get_history() const { return impl_->get_history(); }

  nlohmann::json get_inode_info(inode_view entry) const {
    return impl_->get_inode_info(entry);
  }

  nlohmann::json get_inode_info(inode_view entry, size_t max_chunks) const {
    return impl_->get_inode_info(entry, max_chunks);
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

  std::shared_ptr<internal::filesystem_parser> get_parser() const {
    return impl_->get_parser();
  }

  std::optional<std::string> get_block_category(size_t block_number) const {
    return impl_->get_block_category(block_number);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual int
    check(filesystem_check_level level, size_t num_threads) const = 0;
    virtual void dump(std::ostream& os, fsinfo_options const& opts) const = 0;
    virtual std::string dump(fsinfo_options const& opts) const = 0;
    virtual nlohmann::json info_as_json(fsinfo_options const& opts) const = 0;
    virtual nlohmann::json metadata_as_json() const = 0;
    virtual std::string serialize_metadata_as_json(bool simple) const = 0;
    virtual void
    walk(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual void
    walk_data_order(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual std::optional<inode_view> find(const char* path) const = 0;
    virtual std::optional<inode_view> find(int inode) const = 0;
    virtual std::optional<inode_view>
    find(int inode, const char* name) const = 0;
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
    virtual std::optional<std::pair<inode_view, std::string>>
    readdir(directory_view dir, size_t offset) const = 0;
    virtual size_t dirsize(directory_view dir) const = 0;
    virtual std::string readlink(inode_view entry, readlink_mode mode,
                                 std::error_code& ec) const = 0;
    virtual std::string
    readlink(inode_view entry, readlink_mode mode) const = 0;
    virtual void statvfs(vfs_stat* stbuf) const = 0;
    virtual int open(inode_view entry) const = 0;
    virtual int open(inode_view entry, std::error_code& ec) const = 0;
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
    virtual std::optional<std::span<uint8_t const>> header() const = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_cache_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual size_t num_blocks() const = 0;
    virtual bool has_symlinks() const = 0;
    virtual history const& get_history() const = 0;
    virtual nlohmann::json get_inode_info(inode_view entry) const = 0;
    virtual nlohmann::json
    get_inode_info(inode_view entry, size_t max_chunks) const = 0;
    virtual std::vector<std::string> get_all_block_categories() const = 0;
    virtual std::vector<file_stat::uid_type> get_all_uids() const = 0;
    virtual std::vector<file_stat::gid_type> get_all_gids() const = 0;
    virtual std::shared_ptr<internal::filesystem_parser> get_parser() const = 0;
    virtual std::optional<std::string>
    get_block_category(size_t block_number) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace reader
} // namespace dwarfs
