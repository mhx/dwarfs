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
#include <utility>

#include <folly/Expected.h>
#include <folly/dynamic.h>

#include "dwarfs/block_range.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/metadata_types.h"
#include "dwarfs/options.h"
#include "dwarfs/types.h"

namespace dwarfs {

struct iovec_read_buf;
struct file_stat;
struct vfs_stat;

class category_resolver;
class filesystem_writer;
class history;
class logger;
class mmif;
class os_access;
class performance_monitor;
class progress;

class filesystem_v2 {
 public:
  filesystem_v2() = default;

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

  int check(filesystem_check_level level, size_t num_threads = 0) const {
    return impl_->check(level, num_threads);
  }

  void dump(std::ostream& os, int detail_level) const {
    impl_->dump(os, detail_level);
  }

  std::string dump(int detail_level) const { return impl_->dump(detail_level); }

  folly::dynamic info_as_dynamic(int detail_level) const {
    return impl_->info_as_dynamic(detail_level);
  }

  folly::dynamic metadata_as_dynamic() const {
    return impl_->metadata_as_dynamic();
  }

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

  int getattr(inode_view entry, file_stat* stbuf) const {
    return impl_->getattr(entry, stbuf);
  }

  int access(inode_view entry, int mode, uid_t uid, gid_t gid) const {
    return impl_->access(entry, mode, uid, gid);
  }

  std::optional<directory_view> opendir(inode_view entry) const {
    return impl_->opendir(entry);
  }

  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const {
    return impl_->readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const { return impl_->dirsize(dir); }

  int readlink(inode_view entry, std::string* buf,
               readlink_mode mode = readlink_mode::preferred) const {
    return impl_->readlink(entry, buf, mode);
  }

  folly::Expected<std::string, int>
  readlink(inode_view entry,
           readlink_mode mode = readlink_mode::preferred) const {
    return impl_->readlink(entry, mode);
  }

  int statvfs(vfs_stat* stbuf) const { return impl_->statvfs(stbuf); }

  int open(inode_view entry) const { return impl_->open(entry); }

  ssize_t
  read(uint32_t inode, char* buf, size_t size, file_off_t offset = 0) const {
    return impl_->read(inode, buf, size, offset);
  }

  ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                file_off_t offset = 0) const {
    return impl_->readv(inode, buf, size, offset);
  }

  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(uint32_t inode, size_t size, file_off_t offset = 0) const {
    return impl_->readv(inode, size, offset);
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

  folly::dynamic get_inode_info(inode_view entry) const {
    return impl_->get_inode_info(entry);
  }

  std::vector<std::string> get_all_block_categories() const {
    return impl_->get_all_block_categories();
  }

  void rewrite(progress& prog, filesystem_writer& writer,
               category_resolver const& cat_resolver,
               rewrite_options const& opts) const {
    return impl_->rewrite(prog, writer, cat_resolver, opts);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual int
    check(filesystem_check_level level, size_t num_threads) const = 0;
    virtual void dump(std::ostream& os, int detail_level) const = 0;
    virtual std::string dump(int detail_level) const = 0;
    virtual folly::dynamic info_as_dynamic(int detail_level) const = 0;
    virtual folly::dynamic metadata_as_dynamic() const = 0;
    virtual std::string serialize_metadata_as_json(bool simple) const = 0;
    virtual void
    walk(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual void
    walk_data_order(std::function<void(dir_entry_view)> const& func) const = 0;
    virtual std::optional<inode_view> find(const char* path) const = 0;
    virtual std::optional<inode_view> find(int inode) const = 0;
    virtual std::optional<inode_view>
    find(int inode, const char* name) const = 0;
    virtual int getattr(inode_view entry, file_stat* stbuf) const = 0;
    virtual int
    access(inode_view entry, int mode, uid_t uid, gid_t gid) const = 0;
    virtual std::optional<directory_view> opendir(inode_view entry) const = 0;
    virtual std::optional<std::pair<inode_view, std::string>>
    readdir(directory_view dir, size_t offset) const = 0;
    virtual size_t dirsize(directory_view dir) const = 0;
    virtual int
    readlink(inode_view entry, std::string* buf, readlink_mode mode) const = 0;
    virtual folly::Expected<std::string, int>
    readlink(inode_view entry, readlink_mode mode) const = 0;
    virtual int statvfs(vfs_stat* stbuf) const = 0;
    virtual int open(inode_view entry) const = 0;
    virtual ssize_t
    read(uint32_t inode, char* buf, size_t size, file_off_t offset) const = 0;
    virtual ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                          file_off_t offset) const = 0;
    virtual folly::Expected<std::vector<std::future<block_range>>, int>
    readv(uint32_t inode, size_t size, file_off_t offset) const = 0;
    virtual std::optional<std::span<uint8_t const>> header() const = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_cache_tidy_config(cache_tidy_config const& cfg) = 0;
    virtual size_t num_blocks() const = 0;
    virtual bool has_symlinks() const = 0;
    virtual history const& get_history() const = 0;
    virtual folly::dynamic get_inode_info(inode_view entry) const = 0;
    virtual std::vector<std::string> get_all_block_categories() const = 0;
    virtual void rewrite(progress& prog, filesystem_writer& writer,
                         category_resolver const& cat_resolver,
                         rewrite_options const& opts) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
