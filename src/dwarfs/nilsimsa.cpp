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

#include "dwarfs/nilsimsa.h"
#include "dwarfs/compiler.h"

namespace dwarfs {

namespace {

// Nilsimsa transition table
constexpr std::array<uint8_t, 256> TT53{
    {0x02, 0xD6, 0x9E, 0x6F, 0xF9, 0x1D, 0x04, 0xAB, 0xD0, 0x22, 0x16, 0x1F,
     0xD8, 0x73, 0xA1, 0xAC, 0x3B, 0x70, 0x62, 0x96, 0x1E, 0x6E, 0x8F, 0x39,
     0x9D, 0x05, 0x14, 0x4A, 0xA6, 0xBE, 0xAE, 0x0E, 0xCF, 0xB9, 0x9C, 0x9A,
     0xC7, 0x68, 0x13, 0xE1, 0x2D, 0xA4, 0xEB, 0x51, 0x8D, 0x64, 0x6B, 0x50,
     0x23, 0x80, 0x03, 0x41, 0xEC, 0xBB, 0x71, 0xCC, 0x7A, 0x86, 0x7F, 0x98,
     0xF2, 0x36, 0x5E, 0xEE, 0x8E, 0xCE, 0x4F, 0xB8, 0x32, 0xB6, 0x5F, 0x59,
     0xDC, 0x1B, 0x31, 0x4C, 0x7B, 0xF0, 0x63, 0x01, 0x6C, 0xBA, 0x07, 0xE8,
     0x12, 0x77, 0x49, 0x3C, 0xDA, 0x46, 0xFE, 0x2F, 0x79, 0x1C, 0x9B, 0x30,
     0xE3, 0x00, 0x06, 0x7E, 0x2E, 0x0F, 0x38, 0x33, 0x21, 0xAD, 0xA5, 0x54,
     0xCA, 0xA7, 0x29, 0xFC, 0x5A, 0x47, 0x69, 0x7D, 0xC5, 0x95, 0xB5, 0xF4,
     0x0B, 0x90, 0xA3, 0x81, 0x6D, 0x25, 0x55, 0x35, 0xF5, 0x75, 0x74, 0x0A,
     0x26, 0xBF, 0x19, 0x5C, 0x1A, 0xC6, 0xFF, 0x99, 0x5D, 0x84, 0xAA, 0x66,
     0x3E, 0xAF, 0x78, 0xB3, 0x20, 0x43, 0xC1, 0xED, 0x24, 0xEA, 0xE6, 0x3F,
     0x18, 0xF3, 0xA0, 0x42, 0x57, 0x08, 0x53, 0x60, 0xC3, 0xC0, 0x83, 0x40,
     0x82, 0xD7, 0x09, 0xBD, 0x44, 0x2A, 0x67, 0xA8, 0x93, 0xE0, 0xC2, 0x56,
     0x9F, 0xD9, 0xDD, 0x85, 0x15, 0xB4, 0x8A, 0x27, 0x28, 0x92, 0x76, 0xDE,
     0xEF, 0xF8, 0xB2, 0xB7, 0xC9, 0x3D, 0x45, 0x94, 0x4B, 0x11, 0x0D, 0x65,
     0xD5, 0x34, 0x8B, 0x91, 0x0C, 0xFA, 0x87, 0xE9, 0x7C, 0x5B, 0xB1, 0x4D,
     0xE5, 0xD4, 0xCB, 0x10, 0xA2, 0x17, 0x89, 0xBC, 0xDB, 0xB0, 0xE2, 0x97,
     0x88, 0x52, 0xF7, 0x48, 0xD3, 0x61, 0x2C, 0x3A, 0x2B, 0xD1, 0x8C, 0xFB,
     0xF1, 0xCD, 0xE4, 0x6A, 0xE7, 0xA9, 0xFD, 0xC4, 0x37, 0xC8, 0xD2, 0xF6,
     0xDF, 0x58, 0x72, 0x4E}};

uint8_t tran3(uint8_t a, uint8_t b, uint8_t c, uint8_t n) {
  return ((TT53[(a + n) & 0xFF] ^ TT53[b] * (n + n + 1)) + TT53[c ^ TT53[n]]);
}

} // namespace

class nilsimsa::impl {
 public:
  impl() { acc_.fill(0); }

  void update(uint8_t const* data, size_t size) {
    if (DWARFS_UNLIKELY(size_ < 4)) {
      size_t n = std::min(size, 4 - size_);
      update_slow(data, n);
      data += n;
      size -= n;
      if (size == 0) {
        return;
      }
    }
    update_fast(data, size);
  }

  void finalize(hash_type& hash) const {
    size_t total = 0;

    if (size_ == 3) {
      total = 1;
    } else if (size_ == 4) {
      total = 4;
    } else if (size_ > 4) {
      total = 8 * size_ - 28;
    }

    size_t threshold = total / acc_.size();

    std::fill(hash.begin(), hash.end(), 0);

    for (size_t i = 0; i < acc_.size(); i++) {
      if (acc_[i] > threshold) {
        hash[i >> 6] |= UINT64_C(1) << (i & 0x3F);
      }
    }
  }

 private:
  void update_slow(uint8_t const* data, size_t size) {
    uint_fast8_t w1 = w_[0];
    uint_fast8_t w2 = w_[1];
    uint_fast8_t w3 = w_[2];
    uint_fast8_t w4 = w_[3];

    for (size_t i = 0; i < size; ++i) {
      uint_fast8_t w0 = data[i];

      if (size_ + i > 1) {
        ++acc_[tran3(w0, w1, w2, 0)];

        if (size_ + i > 2) {
          ++acc_[tran3(w0, w1, w3, 1)];
          ++acc_[tran3(w0, w2, w3, 2)];

          if (size_ + i > 3) {
            ++acc_[tran3(w0, w1, w4, 3)];
            ++acc_[tran3(w0, w2, w4, 4)];
            ++acc_[tran3(w0, w3, w4, 5)];
            ++acc_[tran3(w4, w1, w0, 6)];
            ++acc_[tran3(w4, w3, w0, 7)];
          }
        }
      }

      w4 = w3;
      w3 = w2;
      w2 = w1;
      w1 = w0;
    }

    w_[0] = w1;
    w_[1] = w2;
    w_[2] = w3;
    w_[3] = w4;

    size_ += size;
  }

#define DWARFS_NILSIMSA_UPDATE_FAST_IMPL                                       \
  void update_fast(uint8_t const* data, size_t size) {                         \
    uint8_t w1 = w_[0];                                                        \
    uint8_t w2 = w_[1];                                                        \
    uint8_t w3 = w_[2];                                                        \
    uint8_t w4 = w_[3];                                                        \
                                                                               \
    for (size_t i = 0; i < size; ++i) {                                        \
      uint8_t w0 = data[i];                                                    \
                                                                               \
      ++acc_[tran3(w0, w1, w2, 0)];                                            \
      ++acc_[tran3(w0, w1, w3, 1)];                                            \
      ++acc_[tran3(w0, w2, w3, 2)];                                            \
      ++acc_[tran3(w0, w1, w4, 3)];                                            \
      ++acc_[tran3(w0, w2, w4, 4)];                                            \
      ++acc_[tran3(w0, w3, w4, 5)];                                            \
      ++acc_[tran3(w4, w1, w0, 6)];                                            \
      ++acc_[tran3(w4, w3, w0, 7)];                                            \
                                                                               \
      w4 = w3;                                                                 \
      w3 = w2;                                                                 \
      w2 = w1;                                                                 \
      w1 = w0;                                                                 \
    }                                                                          \
                                                                               \
    w_[0] = w1;                                                                \
    w_[1] = w2;                                                                \
    w_[2] = w3;                                                                \
    w_[3] = w4;                                                                \
                                                                               \
    size_ += size;                                                             \
  }                                                                            \
  static_assert(true, "")

#ifdef DWARFS_MULTIVERSIONING
  __attribute__((target("avx"))) DWARFS_NILSIMSA_UPDATE_FAST_IMPL;
  __attribute__((target("default")))
#endif
  DWARFS_NILSIMSA_UPDATE_FAST_IMPL;

  std::array<size_t, 256> acc_;
  std::array<uint_fast8_t, 4> w_;
  size_t size_{0};
};

nilsimsa::nilsimsa()
    : impl_{std::make_unique<impl>()} {}
nilsimsa::~nilsimsa() = default;

void nilsimsa::update(uint8_t const* data, size_t size) {
  impl_->update(data, size);
}

void nilsimsa::finalize(hash_type& hash) const { impl_->finalize(hash); }

#ifdef DWARFS_MULTIVERSIONING
__attribute__((target("popcnt"))) int
nilsimsa::similarity(uint64_t const* a, uint64_t const* b) {
  DWARFS_NILSIMSA_SIMILARITY(return, a, b);
}

__attribute__((target("default")))
#endif
int nilsimsa::similarity(uint64_t const* a, uint64_t const* b) {
  DWARFS_NILSIMSA_SIMILARITY(return, a, b);
}

static_assert(std::is_same_v<unsigned long, uint64_t> ||
              std::is_same_v<unsigned long long, uint64_t>);

} // namespace dwarfs
