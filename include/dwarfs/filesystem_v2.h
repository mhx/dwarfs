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
#include <string>
#include <utility>

#include <sys/types.h>

#include <folly/Expected.h>
#include <folly/dynamic.h>

#include "dwarfs/fstypes.h"
#include "dwarfs/metadata_types.h"

struct stat;
struct statvfs;

namespace dwarfs {

struct cache_tidy_config;
struct filesystem_options;
struct rewrite_options;
struct iovec_read_buf;

class filesystem_writer;
class logger;
class mmif;
class progress;

class filesystem_v2 {
 public:
  filesystem_v2() = default;

  filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm);

  filesystem_v2(logger& lgr, std::shared_ptr<mmif> mm,
                const filesystem_options& options, int inode_offset = 0);

  static void rewrite(logger& lgr, progress& prog, std::shared_ptr<mmif> mm,
                      filesystem_writer& writer, rewrite_options const& opts);

  static int identify(logger& lgr, std::shared_ptr<mmif> mm, std::ostream& os,
                      int detail_level = 0, size_t num_readers = 1,
                      bool check_integrity = false, off_t image_offset = 0);

  static std::optional<folly::ByteRange> header(std::shared_ptr<mmif> mm);

  static std::optional<folly::ByteRange>
  header(std::shared_ptr<mmif> mm, off_t image_offset);

  void dump(std::ostream& os, int detail_level) const {
    impl_->dump(os, detail_level);
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

  int getattr(inode_view entry, struct ::stat* stbuf) const {
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

  int readlink(inode_view entry, std::string* buf) const {
    return impl_->readlink(entry, buf);
  }

  folly::Expected<std::string, int> readlink(inode_view entry) const {
    return impl_->readlink(entry);
  }

  int statvfs(struct ::statvfs* stbuf) const { return impl_->statvfs(stbuf); }

  int open(inode_view entry) const { return impl_->open(entry); }

  ssize_t read(uint32_t inode, char* buf, size_t size, off_t offset = 0) const {
    return impl_->read(inode, buf, size, offset);
  }

  ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                off_t offset = 0) const {
    return impl_->readv(inode, buf, size, offset);
  }

  folly::Expected<std::vector<std::future<block_range>>, int>
  readv(uint32_t inode, size_t size, off_t offset = 0) const {
    return impl_->readv(inode, size, offset);
  }

  std::optional<folly::ByteRange> header() const { return impl_->header(); }

  void set_num_workers(size_t num) { return impl_->set_num_workers(num); }
  void set_cache_tidy_config(cache_tidy_config const& cfg) {
    return impl_->set_cache_tidy_config(cfg);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void dump(std::ostream& os, int detail_level) const = 0;
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
    virtual int getattr(inode_view entry, struct ::stat* stbuf) const = 0;
    virtual int
    access(inode_view entry, int mode, uid_t uid, gid_t gid) const = 0;
    virtual std::optional<directory_view> opendir(inode_view entry) const = 0;
    virtual std::optional<std::pair<inode_view, std::string>>
    readdir(directory_view dir, size_t offset) const = 0;
    virtual size_t dirsize(directory_view dir) const = 0;
    virtual int readlink(inode_view entry, std::string* buf) const = 0;
    virtual folly::Expected<std::string, int>
    readlink(inode_view entry) const = 0;
    virtual int statvfs(struct ::statvfs* stbuf) const = 0;
    virtual int open(inode_view entry) const = 0;
    virtual ssize_t
    read(uint32_t inode, char* buf, size_t size, off_t offset) const = 0;
    virtual ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                          off_t offset) const = 0;
    virtual folly::Expected<std::vector<std::future<block_range>>, int>
    readv(uint32_t inode, size_t size, off_t offset) const = 0;
    virtual std::optional<folly::ByteRange> header() const = 0;
    virtual void set_num_workers(size_t num) = 0;
    virtual void set_cache_tidy_config(cache_tidy_config const& cfg) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
