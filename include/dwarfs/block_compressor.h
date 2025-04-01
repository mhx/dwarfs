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
#include <stdexcept>
#include <string>

#include <dwarfs/byte_buffer.h>
#include <dwarfs/compression.h>
#include <dwarfs/compression_constraints.h>

namespace dwarfs {

class bad_compression_ratio_error : public std::runtime_error {
 public:
  bad_compression_ratio_error()
      : std::runtime_error{"bad compression ratio"} {}
};

class block_compressor {
 public:
  block_compressor() = default;

  explicit block_compressor(std::string const& spec);

  block_compressor(block_compressor const& bc)
      : impl_(bc.impl_->clone()) {}

  block_compressor(block_compressor&& bc) = default;
  block_compressor& operator=(block_compressor&& rhs) = default;

  shared_byte_buffer compress(shared_byte_buffer const& data) const {
    return impl_->compress(data, nullptr);
  }

  shared_byte_buffer
  compress(shared_byte_buffer const& data, std::string const& metadata) const {
    return impl_->compress(data, &metadata);
  }

  compression_type type() const { return impl_->type(); }

  std::string describe() const { return impl_->describe(); }

  std::string metadata_requirements() const {
    return impl_->metadata_requirements();
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const {
    return impl_->get_compression_constraints(metadata);
  }

  explicit operator bool() const { return static_cast<bool>(impl_); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::unique_ptr<impl> clone() const = 0;

    virtual shared_byte_buffer compress(shared_byte_buffer const& data,
                                        std::string const* metadata) const = 0;

    virtual compression_type type() const = 0;
    virtual std::string describe() const = 0;

    virtual std::string metadata_requirements() const = 0;

    virtual compression_constraints
    get_compression_constraints(std::string const& metadata) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
