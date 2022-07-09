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

#include <folly/portability/SysTypes.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <folly/Expected.h>
#include <folly/Range.h>
#include <folly/dynamic.h>

#include "dwarfs/metadata_types.h"

struct stat;
struct statvfs;

namespace dwarfs {

class logger;

struct metadata_options;

struct filesystem_info;

namespace thrift::metadata {
class metadata;
}

class metadata_v2 {
 public:
  metadata_v2() = default;

  metadata_v2(logger& lgr, folly::ByteRange schema, folly::ByteRange data,
              metadata_options const& options, int inode_offset = 0,
              bool force_consistency_check = false);

  metadata_v2& operator=(metadata_v2&&) = default;

  void
  dump(std::ostream& os, int detail_level, filesystem_info const& fsinfo,
       std::function<void(const std::string&, uint32_t)> const& icb) const {
    impl_->dump(os, detail_level, fsinfo, icb);
  }

  folly::dynamic as_dynamic() const { return impl_->as_dynamic(); }

  std::string serialize_as_json(bool simple) const {
    return impl_->serialize_as_json(simple);
  }

  size_t size() const { return impl_->size(); }

  bool empty() const { return !impl_ || impl_->empty(); }

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

  int getattr(inode_view iv, struct ::stat* stbuf) const {
    return impl_->getattr(iv, stbuf);
  }

  std::optional<directory_view> opendir(inode_view iv) const {
    return impl_->opendir(iv);
  }

  std::optional<std::pair<inode_view, std::string>>
  readdir(directory_view dir, size_t offset) const {
    return impl_->readdir(dir, offset);
  }

  size_t dirsize(directory_view dir) const { return impl_->dirsize(dir); }

  int access(inode_view iv, int mode, uid_t uid, gid_t gid) const {
    return impl_->access(iv, mode, uid, gid);
  }

  int open(inode_view iv) const { return impl_->open(iv); }

  int readlink(inode_view iv, std::string* buf) const {
    return impl_->readlink(iv, buf);
  }

  folly::Expected<std::string, int> readlink(inode_view iv) const {
    return impl_->readlink(iv);
  }

  int statvfs(struct ::statvfs* stbuf) const { return impl_->statvfs(stbuf); }

  std::optional<chunk_range> get_chunks(int inode) const {
    return impl_->get_chunks(inode);
  }

  size_t block_size() const { return impl_->block_size(); }

  static std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
  freeze(const thrift::metadata::metadata& data);

  class impl {
   public:
    virtual ~impl() = default;

    virtual void dump(
        std::ostream& os, int detail_level, filesystem_info const& fsinfo,
        std::function<void(const std::string&, uint32_t)> const& icb) const = 0;

    virtual folly::dynamic as_dynamic() const = 0;
    virtual std::string serialize_as_json(bool simple) const = 0;

    virtual size_t size() const = 0;
    virtual bool empty() const = 0;

    virtual void
    walk(std::function<void(dir_entry_view)> const& func) const = 0;

    virtual void
    walk_data_order(std::function<void(dir_entry_view)> const& func) const = 0;

    virtual std::optional<inode_view> find(const char* path) const = 0;
    virtual std::optional<inode_view> find(int inode) const = 0;
    virtual std::optional<inode_view>
    find(int inode, const char* name) const = 0;

    virtual int getattr(inode_view iv, struct ::stat* stbuf) const = 0;

    virtual std::optional<directory_view> opendir(inode_view iv) const = 0;

    virtual std::optional<std::pair<inode_view, std::string>>
    readdir(directory_view dir, size_t offset) const = 0;

    virtual size_t dirsize(directory_view dir) const = 0;

    virtual int access(inode_view iv, int mode, uid_t uid, gid_t gid) const = 0;

    virtual int open(inode_view iv) const = 0;

    virtual int readlink(inode_view iv, std::string* buf) const = 0;

    virtual folly::Expected<std::string, int> readlink(inode_view iv) const = 0;

    virtual int statvfs(struct ::statvfs* stbuf) const = 0;

    virtual std::optional<chunk_range> get_chunks(int inode) const = 0;

    virtual size_t block_size() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
