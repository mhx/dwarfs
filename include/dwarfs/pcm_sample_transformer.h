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

#include <iosfwd>
#include <memory>
#include <span>

namespace dwarfs {

enum class pcm_sample_endianness { Big, Little };
enum class pcm_sample_signedness { Signed, Unsigned };
enum class pcm_sample_padding { Lsb, Msb };

std::ostream& operator<<(std::ostream& os, pcm_sample_endianness e);
std::ostream& operator<<(std::ostream& os, pcm_sample_signedness s);
std::ostream& operator<<(std::ostream& os, pcm_sample_padding p);

template <typename UnpackedType>
class pcm_sample_transformer {
 public:
  pcm_sample_transformer(pcm_sample_endianness end, pcm_sample_signedness sig,
                         pcm_sample_padding pad, int bytes, int bits);

  void unpack(std::span<UnpackedType> dst, std::span<uint8_t const> src) const {
    impl_->unpack(dst, src);
  }

  void pack(std::span<uint8_t> dst, std::span<UnpackedType const> src) const {
    impl_->pack(dst, src);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    unpack(std::span<UnpackedType> dst, std::span<uint8_t const> src) const = 0;
    virtual void
    pack(std::span<uint8_t> dst, std::span<UnpackedType const> src) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
