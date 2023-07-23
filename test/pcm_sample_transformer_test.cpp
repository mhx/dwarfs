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

#include <gtest/gtest.h>

#include <vector>

#include <folly/lang/Bits.h>

#include "dwarfs/pcm_sample_transformer.h"

using namespace dwarfs;

TEST(pcm_sample_transformer, uint8_8bit) {
  std::vector<uint8_t> packed{0, 1, 42, 254, 255};
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  unpacked.resize(packed.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Unsigned,
                                      pcm_sample_padding::Msb, 1, 8);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-128, -127, -86, 126, 127};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, uint16_12bit_be_msb) {
  std::vector<uint16_t> tmp{
      folly::Endian::big<uint16_t>(0),    folly::Endian::big<uint16_t>(1),
      folly::Endian::big<uint16_t>(2047), folly::Endian::big<uint16_t>(2048),
      folly::Endian::big<uint16_t>(2049), folly::Endian::big<uint16_t>(4094),
      folly::Endian::big<uint16_t>(4095),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(2 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Unsigned,
                                      pcm_sample_padding::Msb, 2, 12);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-2048, -2047, -1, 0, 1, 2046, 2047};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, uint16_12bit_be_lsb) {
  std::vector<uint16_t> tmp{
      folly::Endian::big<uint16_t>(0 * 16),
      folly::Endian::big<uint16_t>(1 * 16),
      folly::Endian::big<uint16_t>(2047 * 16),
      folly::Endian::big<uint16_t>(2048 * 16),
      folly::Endian::big<uint16_t>(2049 * 16),
      folly::Endian::big<uint16_t>(4094 * 16),
      folly::Endian::big<uint16_t>(4095 * 16),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(2 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Unsigned,
                                      pcm_sample_padding::Lsb, 2, 12);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-2048, -2047, -1, 0, 1, 2046, 2047};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, int16_16bit_be) {
  std::vector<int16_t> tmp{
      folly::Endian::big<int16_t>(-32768), folly::Endian::big<int16_t>(-32767),
      folly::Endian::big<int16_t>(-1),     folly::Endian::big<int16_t>(0),
      folly::Endian::big<int16_t>(1),      folly::Endian::big<int16_t>(32766),
      folly::Endian::big<int16_t>(32767),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(2 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Signed,
                                      pcm_sample_padding::Msb, 2, 16);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-32768, -32767, -1, 0, 1, 32766, 32767};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, int16_14bit_le_lsb) {
  std::vector<int16_t> tmp{
      folly::Endian::little<int16_t>(-8192 * 4),
      folly::Endian::little<int16_t>(-8191 * 4),
      folly::Endian::little<int16_t>(-1 * 4),
      folly::Endian::little<int16_t>(0 * 4),
      folly::Endian::little<int16_t>(1 * 4),
      folly::Endian::little<int16_t>(8190 * 4),
      folly::Endian::little<int16_t>(8191 * 4),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(2 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Little,
                                      pcm_sample_signedness::Signed,
                                      pcm_sample_padding::Lsb, 2, 14);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-8192, -8191, -1, 0, 1, 8190, 8191};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, int32_24bit_be_lsb) {
  std::vector<int32_t> tmp{
      folly::Endian::big<int32_t>(-8388608 * 256),
      folly::Endian::big<int32_t>(-8388607 * 256),
      folly::Endian::big<int32_t>(-1 * 256),
      folly::Endian::big<int32_t>(0 * 256),
      folly::Endian::big<int32_t>(1 * 256),
      folly::Endian::big<int32_t>(8388606 * 256),
      folly::Endian::big<int32_t>(8388607 * 256),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(4 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Signed,
                                      pcm_sample_padding::Lsb, 4, 24);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-8388608, -8388607, -1, 0, 1, 8388606, 8388607};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, int32_24bit_le_msb) {
  std::vector<int32_t> tmp{
      folly::Endian::little<int32_t>(-8388608),
      folly::Endian::little<int32_t>(-8388607),
      folly::Endian::little<int32_t>(-1),
      folly::Endian::little<int32_t>(0),
      folly::Endian::little<int32_t>(1),
      folly::Endian::little<int32_t>(8388606),
      folly::Endian::little<int32_t>(8388607),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(4 * tmp.size());

  ::memcpy(packed.data(), tmp.data(), packed.size());

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Little,
                                      pcm_sample_signedness::Signed,
                                      pcm_sample_padding::Msb, 4, 24);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-8388608, -8388607, -1, 0, 1, 8388606, 8388607};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}

TEST(pcm_sample_transformer, int24_20bit_be_lsb) {
  std::vector<int32_t> const tmp{
      folly::Endian::big<int32_t>(-524288 * 16),
      folly::Endian::big<int32_t>(-524287 * 16),
      folly::Endian::big<int32_t>(-1 * 16),
      folly::Endian::big<int32_t>(0 * 16),
      folly::Endian::big<int32_t>(1 * 16),
      folly::Endian::big<int32_t>(524286 * 16),
      folly::Endian::big<int32_t>(524287 * 16),
  };
  std::vector<uint8_t> packed;
  std::vector<int32_t> unpacked;
  std::vector<uint8_t> repacked;

  packed.resize(3 * tmp.size());

  for (size_t i = 0; i < tmp.size(); ++i) {
    ::memcpy(packed.data() + 3 * i,
             reinterpret_cast<uint8_t const*>(&tmp[i]) + 1, 3);
  }

  unpacked.resize(tmp.size());
  repacked.resize(packed.size());

  pcm_sample_transformer<int32_t> xfm(pcm_sample_endianness::Big,
                                      pcm_sample_signedness::Signed,
                                      pcm_sample_padding::Lsb, 3, 20);

  xfm.unpack(unpacked, packed);
  xfm.pack(repacked, unpacked);

  std::vector<int32_t> ref{-524288, -524287, -1, 0, 1, 524286, 524287};

  EXPECT_EQ(ref, unpacked);
  EXPECT_EQ(packed, repacked);
}
