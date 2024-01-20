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

#include <cassert>
#include <ostream>
#include <type_traits>

#include <fmt/format.h>

#include <folly/lang/Assume.h>

#include "dwarfs/pcm_sample_transformer.h"

namespace dwarfs {

namespace {

template <typename UnpackedType>
class basic_pcm_sample_transformer {
 public:
  using uint_type = std::make_unsigned_t<UnpackedType>;

  template <pcm_sample_endianness End, pcm_sample_signedness Sig,
            pcm_sample_padding Pad, int Bytes>
  static constexpr void
  unpack(UnpackedType* dst, uint8_t const* src, int bits) {
    uint_type tmp;
    if constexpr (End == pcm_sample_endianness::Big) {
      if constexpr (Bytes == 1) {
        tmp = (static_cast<uint_type>(src[0]) << 0);
      }
      if constexpr (Bytes == 2) {
        tmp = (static_cast<uint_type>(src[0]) << 8) |
              (static_cast<uint_type>(src[1]) << 0);
      }
      if constexpr (Bytes == 3) {
        tmp = (static_cast<uint_type>(src[0]) << 16) |
              (static_cast<uint_type>(src[1]) << 8) |
              (static_cast<uint_type>(src[2]) << 0);
      }
      if constexpr (Bytes == 4) {
        tmp = (static_cast<uint_type>(src[0]) << 24) |
              (static_cast<uint_type>(src[1]) << 16) |
              (static_cast<uint_type>(src[2]) << 8) |
              (static_cast<uint_type>(src[3]) << 0);
      }
    } else {
      if constexpr (Bytes == 1) {
        tmp = (static_cast<uint_type>(src[0]) << 0);
      }
      if constexpr (Bytes == 2) {
        tmp = (static_cast<uint_type>(src[0]) << 0) |
              (static_cast<uint_type>(src[1]) << 8);
      }
      if constexpr (Bytes == 3) {
        tmp = (static_cast<uint_type>(src[0]) << 0) |
              (static_cast<uint_type>(src[1]) << 8) |
              (static_cast<uint_type>(src[2]) << 16);
      }
      if constexpr (Bytes == 4) {
        tmp = (static_cast<uint_type>(src[0]) << 0) |
              (static_cast<uint_type>(src[1]) << 8) |
              (static_cast<uint_type>(src[2]) << 16) |
              (static_cast<uint_type>(src[3]) << 24);
      }
    }
    *dst = unpack_native<Sig, Pad, Bytes>(tmp, bits);
  }

  template <pcm_sample_endianness End, pcm_sample_signedness Sig,
            pcm_sample_padding Pad, int Bytes>
  static constexpr void pack(uint8_t* dst, UnpackedType const* src, int bits) {
    auto tmp = pack_native<Sig, Pad, Bytes>(*src, bits);
    if constexpr (End == pcm_sample_endianness::Big) {
      if constexpr (Bytes == 1) {
        dst[0] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
      }
      if constexpr (Bytes == 2) {
        dst[0] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
      }
      if constexpr (Bytes == 3) {
        dst[0] = static_cast<uint8_t>((tmp >> 16) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
        dst[2] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
      }
      if constexpr (Bytes == 4) {
        dst[0] = static_cast<uint8_t>((tmp >> 24) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 16) & 0xFF);
        dst[2] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
        dst[3] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
      }
    } else {
      if constexpr (Bytes == 1) {
        dst[0] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
      }
      if constexpr (Bytes == 2) {
        dst[0] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
      }
      if constexpr (Bytes == 3) {
        dst[0] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
        dst[2] = static_cast<uint8_t>((tmp >> 16) & 0xFF);
      }
      if constexpr (Bytes == 4) {
        dst[0] = static_cast<uint8_t>((tmp >> 0) & 0xFF);
        dst[1] = static_cast<uint8_t>((tmp >> 8) & 0xFF);
        dst[2] = static_cast<uint8_t>((tmp >> 16) & 0xFF);
        dst[3] = static_cast<uint8_t>((tmp >> 24) & 0xFF);
      }
    }
  }

 private:
  template <pcm_sample_signedness Sig, pcm_sample_padding Pad, int Bytes>
  static constexpr UnpackedType unpack_native(uint_type src, int bits) {
    if constexpr (Pad == pcm_sample_padding::Lsb) {
      src >>= (8 * Bytes - bits);
    }

    if constexpr (Sig == pcm_sample_signedness::Signed) {
      if (bits < 8 * static_cast<int>(sizeof(uint_type))) {
        if (src & (1 << (bits - 1))) {
          src |= (~static_cast<uint_type>(0)) << bits;
        }
      }

      return static_cast<UnpackedType>(src);
    } else {
      return static_cast<UnpackedType>(src) - (1 << (bits - 1));
    }
  }

  template <pcm_sample_signedness Sig, pcm_sample_padding Pad, int Bytes>
  static constexpr uint_type pack_native(UnpackedType src, int bits) {
    if constexpr (Sig == pcm_sample_signedness::Unsigned) {
      src += (1 << (bits - 1));
    }

    if constexpr (Pad == pcm_sample_padding::Lsb) {
      return static_cast<uint_type>(src << (8 * Bytes - bits));
    } else {
      return static_cast<uint_type>(src);
    }
  }
};

template <typename UnpackedType, pcm_sample_endianness End,
          pcm_sample_signedness Sig, pcm_sample_padding Pad, int Bytes,
          int Bits>
class pcm_sample_transformer_fixed final
    : public pcm_sample_transformer<UnpackedType>::impl {
 public:
  using basic_transformer = basic_pcm_sample_transformer<UnpackedType>;

  void unpack(std::span<UnpackedType> dst,
              std::span<uint8_t const> src) const override {
    assert(Bytes * dst.size() == src.size());
    for (size_t i = 0; i < dst.size(); ++i) {
      basic_transformer::template unpack<End, Sig, Pad, Bytes>(
          &dst[i], &src[Bytes * i], Bits);
    }
  }

  void pack(std::span<uint8_t> dst,
            std::span<UnpackedType const> src) const override {
    assert(dst.size() == Bytes * src.size());
    for (size_t i = 0; i < src.size(); ++i) {
      basic_transformer::template pack<End, Sig, Pad, Bytes>(&dst[Bytes * i],
                                                             &src[i], Bits);
    }
  }
};

template <typename UnpackedType, pcm_sample_endianness End,
          pcm_sample_signedness Sig, pcm_sample_padding Pad, int Bytes>
class pcm_sample_transformer_generic final
    : public pcm_sample_transformer<UnpackedType>::impl {
 public:
  using basic_transformer = basic_pcm_sample_transformer<UnpackedType>;

  explicit pcm_sample_transformer_generic(int bits)
      : bits_{bits} {}

  void unpack(std::span<UnpackedType> dst,
              std::span<uint8_t const> src) const override {
    assert(Bytes * dst.size() == src.size());
    for (size_t i = 0; i < dst.size(); ++i) {
      basic_transformer::template unpack<End, Sig, Pad, Bytes>(
          &dst[i], &src[Bytes * i], bits_);
    }
  }

  void pack(std::span<uint8_t> dst,
            std::span<UnpackedType const> src) const override {
    assert(dst.size() == Bytes * src.size());
    for (size_t i = 0; i < src.size(); ++i) {
      basic_transformer::template pack<End, Sig, Pad, Bytes>(&dst[Bytes * i],
                                                             &src[i], bits_);
    }
  }

 private:
  int bits_;
};

template <typename UnpackedType, pcm_sample_endianness End,
          pcm_sample_signedness Sig, pcm_sample_padding Pad, int Bytes>
std::unique_ptr<typename pcm_sample_transformer<UnpackedType>::impl>
make_pcm_sample_transformer(int bits) {
  static_assert(1 <= Bytes && Bytes <= 4);

  if constexpr (Bytes == 1) {
    if (bits == 8) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 1, 8>>();
    }

    return std::make_unique<
        pcm_sample_transformer_generic<UnpackedType, End, Sig, Pad, 1>>(bits);
  }

  if constexpr (Bytes == 2) {
    if (bits == 16) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 2, 16>>();
    }

    return std::make_unique<
        pcm_sample_transformer_generic<UnpackedType, End, Sig, Pad, 2>>(bits);
  }

  if constexpr (Bytes == 3) {
    if (bits == 20) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 3, 20>>();
    }

    if (bits == 24) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 3, 24>>();
    }

    return std::make_unique<
        pcm_sample_transformer_generic<UnpackedType, End, Sig, Pad, 3>>(bits);
  }

  if constexpr (Bytes == 4) {
    if (bits == 20) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 4, 20>>();
    }

    if (bits == 24) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 4, 24>>();
    }

    if (bits == 32) {
      return std::make_unique<
          pcm_sample_transformer_fixed<UnpackedType, End, Sig, Pad, 4, 32>>();
    }

    return std::make_unique<
        pcm_sample_transformer_generic<UnpackedType, End, Sig, Pad, 4>>(bits);
  }
}

template <typename UnpackedType, pcm_sample_endianness End,
          pcm_sample_signedness Sig, pcm_sample_padding Pad>
std::unique_ptr<typename pcm_sample_transformer<UnpackedType>::impl>
make_pcm_sample_transformer(int bytes, int bits) {
  switch (bytes) {
  case 1:
    return make_pcm_sample_transformer<UnpackedType, End, Sig, Pad, 1>(bits);
  case 2:
    return make_pcm_sample_transformer<UnpackedType, End, Sig, Pad, 2>(bits);
  case 3:
    return make_pcm_sample_transformer<UnpackedType, End, Sig, Pad, 3>(bits);
  case 4:
    return make_pcm_sample_transformer<UnpackedType, End, Sig, Pad, 4>(bits);
  default:
    throw std::runtime_error(
        fmt::format("unsupported number of bytes per sample: {}", bytes));
  }
}

template <typename UnpackedType, pcm_sample_endianness End,
          pcm_sample_signedness Sig>
std::unique_ptr<typename pcm_sample_transformer<UnpackedType>::impl>
make_pcm_sample_transformer(pcm_sample_padding pad, int bytes, int bits) {
  switch (pad) {
  case pcm_sample_padding::Lsb:
    return make_pcm_sample_transformer<UnpackedType, End, Sig,
                                       pcm_sample_padding::Lsb>(bytes, bits);
  case pcm_sample_padding::Msb:
    return make_pcm_sample_transformer<UnpackedType, End, Sig,
                                       pcm_sample_padding::Msb>(bytes, bits);
  }

  folly::assume_unreachable();
}

template <typename UnpackedType, pcm_sample_endianness End>
std::unique_ptr<typename pcm_sample_transformer<UnpackedType>::impl>
make_pcm_sample_transformer(pcm_sample_signedness sig, pcm_sample_padding pad,
                            int bytes, int bits) {
  switch (sig) {
  case pcm_sample_signedness::Signed:
    return make_pcm_sample_transformer<UnpackedType, End,
                                       pcm_sample_signedness::Signed>(
        pad, bytes, bits);
  case pcm_sample_signedness::Unsigned:
    return make_pcm_sample_transformer<UnpackedType, End,
                                       pcm_sample_signedness::Unsigned>(
        pad, bytes, bits);
  }

  folly::assume_unreachable();
}

template <typename UnpackedType>
std::unique_ptr<typename pcm_sample_transformer<UnpackedType>::impl>
make_pcm_sample_transformer(pcm_sample_endianness end,
                            pcm_sample_signedness sig, pcm_sample_padding pad,
                            int bytes, int bits) {
  assert(bits <= 8 * bytes);

  switch (end) {
  case pcm_sample_endianness::Big:
    return make_pcm_sample_transformer<UnpackedType,
                                       pcm_sample_endianness::Big>(sig, pad,
                                                                   bytes, bits);
  case pcm_sample_endianness::Little:
    return make_pcm_sample_transformer<UnpackedType,
                                       pcm_sample_endianness::Little>(
        sig, pad, bytes, bits);
  }

  folly::assume_unreachable();
}

} // namespace

template <typename UnpackedType>
pcm_sample_transformer<UnpackedType>::pcm_sample_transformer(
    pcm_sample_endianness end, pcm_sample_signedness sig,
    pcm_sample_padding pad, int bytes, int bits)
    : impl_{make_pcm_sample_transformer<UnpackedType>(end, sig, pad, bytes,
                                                      bits)} {}

template class pcm_sample_transformer<int32_t>;

std::ostream& operator<<(std::ostream& os, pcm_sample_endianness e) {
  os << (e == pcm_sample_endianness::Big ? "big-endian" : "little-endian");
  return os;
}

std::ostream& operator<<(std::ostream& os, pcm_sample_signedness s) {
  os << (s == pcm_sample_signedness::Signed ? "signed" : "unsigned");
  return os;
}

std::ostream& operator<<(std::ostream& os, pcm_sample_padding p) {
  os << (p == pcm_sample_padding::Lsb ? "lsb-padded" : "msb-padded");
  return os;
}

} // namespace dwarfs
