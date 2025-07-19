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
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include <dwarfs/byte_buffer.h>

namespace dwarfs {

class logger;

namespace thrift::metadata {
class metadata;
}

namespace writer::internal {

class metadata_freezer {
 public:
  metadata_freezer(logger& lgr);
  ~metadata_freezer();

  std::pair<shared_byte_buffer, shared_byte_buffer>
  freeze(thrift::metadata::metadata const& data) const {
    return impl_->freeze(data);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::pair<shared_byte_buffer, shared_byte_buffer>
    freeze(thrift::metadata::metadata const& data) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer::internal

} // namespace dwarfs
