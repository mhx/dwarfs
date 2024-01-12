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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <unordered_map>

#include <boost/algorithm/hex.hpp>

#include "dwarfs/checksum.h"

using namespace dwarfs;

namespace {

constexpr std::string_view const payload{"Hello, World!"};

std::unordered_map<checksum::algorithm, std::string> ref_digests_enum{
    {checksum::algorithm::SHA2_512_256,
     "0686F0A605973DC1BF035D1E2B9BAD1985A0BFF712DDD88ABD8D2593E5F99030"},
    {checksum::algorithm::XXH3_64, "AA0266615F5D4160"},
    {checksum::algorithm::XXH3_128, "9553D72C8403DB7750DD474484F21D53"},
};

std::unordered_map<std::string, std::string> ref_digests_str{
    {"blake2b512",
     "7DFDB888AF71EAE0E6A6B751E8E3413D767EF4FA52A7993DAA9EF097F7AA3D949199C113C"
     "AA37C94F80CF3B22F7D9D6E4F5DEF4FF927830CFFE4857C34BE3D89"},
    {"blake2s256",
     "EC9DB904D636EF61F1421B2BA47112A4FA6B8964FD4A0A514834455C21DF7812"},
    {"md5", "65A8E27D8879283831B664BD8B7F0AD4"},
    {"md5-sha1", "65A8E27D8879283831B664BD8B7F0AD40A0A9F2A6772942557AB5355D76AF"
                 "442F8F65E01"},
    {"ripemd160", "527A6A4B9A6DA75607546842E0E00105350B1AAF"},
    {"sha1", "0A0A9F2A6772942557AB5355D76AF442F8F65E01"},
    {"sha224", "72A23DFA411BA6FDE01DBFABF3B00A709C93EBF273DC29E2D8B261FF"},
    {"sha256",
     "DFFD6021BB2BD5B0AF676290809EC3A53191DD81C7F70A4B28688A362182986F"},
    {"sha3-224", "853048FB8B11462B6100385633C0CC8DCDC6E2B8E376C28102BC84F2"},
    {"sha3-256",
     "1AF17A664E3FA8E419B8BA05C2A173169DF76162A5A286E0C405B460D478F7EF"},
    {"sha3-384", "AA9AD8A49F31D2DDCABBB7010A1566417CFF803FEF50EBA239558826F872E"
                 "468C5743E7F026B0A8E5B2D7A1CC465CDBE"},
    {"sha3-512",
     "38E05C33D7B067127F217D8C856E554FCFF09C9320B8A5979CE2FF5D95DD27BA35D1FBA50"
     "C562DFD1D6CC48BC9C5BAA4390894418CC942D968F97BCB659419ED"},
    {"sha384", "5485CC9B3365B4305DFB4E8337E0A598A574F8242BF17289E0DD6C20A3CD44A"
               "089DE16AB4AB308F63E44B1170EB5F515"},
    {"sha512",
     "374D794A95CDCFD8B35993185FEF9BA368F160D8DAF432D08BA9F1ED1E5ABE6CC69291E0F"
     "A2FE0006A52570EF18C19DEF4E617C33CE52EF0A6E5FBE318CB0387"},
    {"sha512-224", "766745F058E8A0438F19DE48AE56EA5F123FE738AF39BCA050A7547A"},
    {"sha512-256",
     "0686F0A605973DC1BF035D1E2B9BAD1985A0BFF712DDD88ABD8D2593E5F99030"},
    {"shake128", "2BF5E6DEE6079FAD604F573194BA8426"},
    {"shake256",
     "B3BE97BFD978833A65588CEAE8A34CF59E95585AF62063E6B89D0789F372424E"},
    {"sm3", "7ED26CBF0BEE4CA7D55C1E64714C4AA7D1F163089EF5CEB603CD102C81FBCBC5"},
    {"xxh3-128", "9553D72C8403DB7750DD474484F21D53"},
    {"xxh3-64", "AA0266615F5D4160"},
};

} // namespace

class checksum_test_str : public ::testing::TestWithParam<std::string> {};

TEST_P(checksum_test_str, end_to_end) {
  auto alg = GetParam();

  std::vector<uint8_t> digest;

  {
    checksum cs(alg);
    cs.update(payload.data(), payload.size());
    digest.resize(cs.digest_size());
    cs.finalize(digest.data());
  }

  auto hexdigest =
      boost::algorithm::hex(std::string(digest.begin(), digest.end()));

  EXPECT_TRUE(checksum::verify(alg, payload.data(), payload.size(),
                               digest.data(), digest.size()));

  if (auto it = ref_digests_str.find(alg); it != ref_digests_str.end()) {
    EXPECT_EQ(it->second, hexdigest) << alg;
  }
}

INSTANTIATE_TEST_SUITE_P(checksum_test, checksum_test_str,
                         ::testing::ValuesIn(checksum::available_algorithms()));

class checksum_test_enum
    : public ::testing::TestWithParam<checksum::algorithm> {};

TEST_P(checksum_test_enum, end_to_end) {
  auto alg = GetParam();

  std::vector<uint8_t> digest;

  {
    checksum cs(alg);
    cs.update(payload.data(), payload.size());
    digest.resize(cs.digest_size());
    cs.finalize(digest.data());
  }

  auto hexdigest =
      boost::algorithm::hex(std::string(digest.begin(), digest.end()));

  auto it = ref_digests_enum.find(alg);

  ASSERT_FALSE(it == ref_digests_enum.end());

  EXPECT_TRUE(checksum::verify(alg, payload.data(), payload.size(),
                               digest.data(), digest.size()));

  EXPECT_EQ(it->second, hexdigest) << alg;
}

INSTANTIATE_TEST_SUITE_P(checksum_test, checksum_test_enum,
                         ::testing::ValuesIn({checksum::algorithm::SHA2_512_256,
                                              checksum::algorithm::XXH3_64,
                                              checksum::algorithm::XXH3_128}));
