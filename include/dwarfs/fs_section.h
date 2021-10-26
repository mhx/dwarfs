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

#include <memory>
#include <string>

#include <folly/Range.h>

#include "dwarfs/fstypes.h"

namespace dwarfs {

class mmif;

class fs_section {
 public:
  fs_section(mmif& mm, size_t offset, int version);
  fs_section(std::shared_ptr<mmif> mm, section_type type, size_t offset,
             size_t size, int version);

  size_t start() const { return impl_->start(); }
  size_t length() const { return impl_->length(); }
  compression_type compression() const { return impl_->compression(); }
  section_type type() const { return impl_->type(); }
  std::string name() const { return impl_->name(); }
  std::string description() const { return impl_->description(); }
  bool check_fast(mmif& mm) const { return impl_->check_fast(mm); }
  bool verify(mmif& mm) const { return impl_->verify(mm); }
  folly::ByteRange data(mmif& mm) const { return impl_->data(mm); }

  size_t end() const { return start() + length(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual size_t start() const = 0;
    virtual size_t length() const = 0;
    virtual compression_type compression() const = 0;
    virtual section_type type() const = 0;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual bool check_fast(mmif& mm) const = 0;
    virtual bool verify(mmif& mm) const = 0;
    virtual folly::ByteRange data(mmif& mm) const = 0;
  };

 private:
  std::shared_ptr<impl const> impl_;
};

} // namespace dwarfs
