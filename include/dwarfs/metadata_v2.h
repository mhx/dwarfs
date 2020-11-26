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
#include <functional>
#include <memory>
#include <vector>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include "fstypes.h"
#include "logger.h"

namespace dwarfs {

class metadata_v2 {
 public:
  metadata_v2() = default;

  metadata_v2(logger& lgr, std::vector<uint8_t>&& data,
              const struct ::stat* defaults);

  metadata_v2& operator=(metadata_v2&&) = default;

  void
  dump(std::ostream& os,
       std::function<void(const std::string&, uint32_t)> const& icb) const {
    impl_->dump(os, icb);
  }

#if 0
  static void get_stat_defaults(struct ::stat* defaults);

  size_t size() const { return impl_->size(); }

  bool empty() const { return !impl_ || impl_->empty(); }

  size_t block_size() const { return impl_->block_size(); }

  unsigned block_size_bits() const { return impl_->block_size_bits(); }

  void walk(std::function<void(const dir_entry*)> const& func) const {
    impl_->walk(func);
  }

  const dir_entry* find(const char* path) const { return impl_->find(path); }

  const dir_entry* find(int inode) const { return impl_->find(inode); }

  const dir_entry* find(int inode, const char* name) const {
    return impl_->find(inode, name);
  }

  int getattr(const dir_entry* de, struct ::stat* stbuf) const {
    return impl_->getattr(de, stbuf);
  }

  int access(const dir_entry* de, int mode, uid_t uid, gid_t gid) const {
    return impl_->access(de, mode, uid, gid);
  }

  const directory* opendir(const dir_entry* de) const {
    return impl_->opendir(de);
  }

  const dir_entry*
  readdir(const directory* d, size_t offset, std::string* name) const {
    return impl_->readdir(d, offset, name);
  }

  size_t dirsize(const directory* d) const { return impl_->dirsize(d); }

  int readlink(const dir_entry* de, char* buf, size_t size) const {
    return impl_->readlink(de, buf, size);
  }

  int readlink(const dir_entry* de, std::string* buf) const {
    return impl_->readlink(de, buf);
  }

  int statvfs(struct ::statvfs* stbuf) const { return impl_->statvfs(stbuf); }

  int open(const dir_entry* de) const { return impl_->open(de); }

  const chunk_type* get_chunks(int inode, size_t& num) const {
    return impl_->get_chunks(inode, num);
  }
#endif

  class impl {
   public:
    virtual ~impl() = default;

    virtual void dump(
        std::ostream& os,
        std::function<void(const std::string&, uint32_t)> const& icb) const = 0;
#if 0
    virtual size_t size() const = 0;
    virtual bool empty() const = 0;
    virtual size_t block_size() const = 0;
    virtual unsigned block_size_bits() const = 0;
    virtual void
    walk(std::function<void(const dir_entry*)> const& func) const = 0;
    virtual const dir_entry* find(const char* path) const = 0;
    virtual const dir_entry* find(int inode) const = 0;
    virtual const dir_entry* find(int inode, const char* name) const = 0;
    virtual int getattr(const dir_entry* de, struct ::stat* stbuf) const = 0;
    virtual int
    access(const dir_entry* de, int mode, uid_t uid, gid_t gid) const = 0;
    virtual const directory* opendir(const dir_entry* de) const = 0;
    virtual const dir_entry*
    readdir(const directory* d, size_t offset, std::string* name) const = 0;
    virtual size_t dirsize(const directory* d) const = 0;
    virtual int readlink(const dir_entry* de, char* buf, size_t size) const = 0;
    virtual int readlink(const dir_entry* de, std::string* buf) const = 0;
    virtual int statvfs(struct ::statvfs* stbuf) const = 0;
    virtual int open(const dir_entry* de) const = 0;
    virtual const chunk_type* get_chunks(int inode, size_t& num) const = 0;
#endif
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
