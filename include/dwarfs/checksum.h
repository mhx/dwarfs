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
#include <memory>
#include <stdexcept>

#include "dwarfs/error.h"

namespace dwarfs {

class checksum {
 public:
  enum class algorithm {
    SHA1,
    SHA2_512_256,
    XXH3_64,
    XXH3_128,
  };

  static constexpr size_t digest_size(algorithm alg) {
    switch (alg) {
    case algorithm::SHA1:
      return 20;
    case algorithm::SHA2_512_256:
      return 32;
    case algorithm::XXH3_64:
      return 8;
    case algorithm::XXH3_128:
      return 16;
    }
    DWARFS_CHECK(false, "unknown algorithm");
  }

  static bool
  compute(algorithm alg, void const* data, size_t size, void* digest);

  static bool
  verify(algorithm alg, void const* data, size_t size, void const* digest);

  checksum(algorithm alg);

  checksum& update(void const* data, size_t size) {
    impl_->update(data, size);
    return *this;
  }

  bool finalize(void* digest) const { return impl_->finalize(digest); }

  bool verify(void const* digest) const;

  algorithm type() const { return alg_; }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void update(void const* data, size_t size) = 0;
    virtual bool finalize(void* digest) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
  algorithm const alg_;
};

} // namespace dwarfs
