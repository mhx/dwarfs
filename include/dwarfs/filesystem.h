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

#include <exception>
#include <functional>
#include <memory>
#include <ostream>
#include <string>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include "logger.h"
#include "mmif.h"

namespace dwarfs {

struct iovec_read_buf;

class error : public std::exception {
 public:
  error(const std::string& str, int err_no) noexcept
      : what_(str)
      , errno_(err_no) {}

  error(const error& e) noexcept
      : what_(e.what_)
      , errno_(e.errno_) {}

  error& operator=(const error& e) noexcept {
    if (&e != this) {
      what_ = e.what_;
      errno_ = e.errno_;
    }
    return *this;
  }

  const char* what() const noexcept override { return what_.c_str(); }

  int get_errno() const { return errno_; }

 private:
  std::string what_;
  int errno_;
};

struct block_cache_options;
struct dir_entry;
struct directory;

class filesystem_writer;
class progress;

class filesystem {
 public:
  filesystem(logger& lgr, std::shared_ptr<mmif> mm,
             const block_cache_options& bc_options,
             const struct ::stat* stat_defaults = nullptr,
             int inode_offset = 0);

  static void rewrite(logger& lgr, progress& prog, std::shared_ptr<mmif> mm,
                      filesystem_writer& writer);

  static void identify(logger& lgr, std::shared_ptr<mmif> mm, std::ostream& os);

  void dump(std::ostream& os) const { impl_->dump(os); }

  void walk(std::function<void(const dir_entry*)> const& func) {
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

  ssize_t read(uint32_t inode, char* buf, size_t size, off_t offset) const {
    return impl_->read(inode, buf, size, offset);
  }

  ssize_t
  readv(uint32_t inode, iovec_read_buf& buf, size_t size, off_t offset) const {
    return impl_->readv(inode, buf, size, offset);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void dump(std::ostream& os) const = 0;
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
    virtual ssize_t
    read(uint32_t inode, char* buf, size_t size, off_t offset) const = 0;
    virtual ssize_t readv(uint32_t inode, iovec_read_buf& buf, size_t size,
                          off_t offset) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
