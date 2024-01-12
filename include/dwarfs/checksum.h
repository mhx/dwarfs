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
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "dwarfs/error.h"

namespace dwarfs {

class checksum {
 public:
  enum class algorithm {
    SHA2_512_256,
    XXH3_64,
    XXH3_128,
  };

  static bool is_available(std::string const& algo);
  static std::vector<std::string> available_algorithms();

  static bool verify(algorithm alg, void const* data, size_t size,
                     void const* digest, size_t digest_size);
  static bool verify(std::string const& alg, void const* data, size_t size,
                     void const* digest, size_t digest_size);

  checksum(algorithm alg);
  checksum(std::string const& alg);

  checksum& update(void const* data, size_t size) {
    impl_->update(data, size);
    return *this;
  }

  bool finalize(void* digest) const { return impl_->finalize(digest); }

  size_t digest_size() const { return impl_->digest_size(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void update(void const* data, size_t size) = 0;
    virtual bool finalize(void* digest) = 0;
    virtual size_t digest_size() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

std::ostream& operator<<(std::ostream& os, checksum::algorithm alg);

} // namespace dwarfs
