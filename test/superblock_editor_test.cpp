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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <ostream>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/checksum.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/superblock.h>
#include <dwarfs/superblock_editor.h>

using namespace dwarfs;
using namespace std::string_view_literals;

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace {

// The tests deliberately build superblocks byte by byte instead of reusing
// superblock_v1. That way they also pin down the on-disk layout and are
// independent of both the host's endianness and of any future change to
// the struct.

constexpr std::size_t kHdrSize{64};
constexpr std::size_t kPayloadSize{192};
constexpr std::size_t kSbSize{kHdrSize + kPayloadSize};

static_assert(kSbSize == superblock_editor::min_section_size());

// section header
constexpr std::size_t kOffMagic{0};
constexpr std::size_t kOffMajor{6};
constexpr std::size_t kOffMinor{7};
constexpr std::size_t kOffSha2{8};
constexpr std::size_t kOffXxh3{40};
constexpr std::size_t kOffNumber{48};
constexpr std::size_t kOffType{52};
constexpr std::size_t kOffCompression{54};
constexpr std::size_t kOffLength{56};

// superblock payload
constexpr std::size_t kOffSbMajor{64};
constexpr std::size_t kOffSbMinor{65};
constexpr std::size_t kOffDigestAlgo{66};
constexpr std::size_t kOffDigestScheme{67};
constexpr std::size_t kOffAlignLog2{68};
constexpr std::size_t kOffReserved1{69};
constexpr std::size_t kOffFsSize{72};
constexpr std::size_t kOffUuid{80};
constexpr std::size_t kOffLabel{96};
constexpr std::size_t kOffAttrDigest{160};
constexpr std::size_t kOffTreeDigest{192};
constexpr std::size_t kOffReserved2{224};

constexpr std::size_t kUuidSize{16};
constexpr std::size_t kLabelSize{64};
constexpr std::size_t kMaxLabelLength{63};
constexpr std::size_t kDigestSize{32};
constexpr std::size_t kReserved1Size{3};
constexpr std::size_t kReserved2Size{32};

constexpr std::uint8_t kAlignLog2{12};
constexpr std::uint64_t kAlignment{std::uint64_t{1} << kAlignLog2};

constexpr std::uint8_t kScheme{1};
constexpr auto kAlgo{digest_algorithm::BLAKE3_256};
constexpr auto kUnknownAlgo{static_cast<digest_algorithm>(0xfe)};

constexpr auto kBinIn{std::ios::in | std::ios::binary};
constexpr auto kBinInOut{std::ios::in | std::ios::out | std::ios::binary};

using uuid_bytes = std::array<std::uint8_t, kUuidSize>;
using digest_bytes = std::array<std::uint8_t, kDigestSize>;

// version 4, RFC 9562 variant
constexpr uuid_bytes kUuid{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0x4c, 0xde,
                           0x8f, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
constexpr std::string_view kUuidStr{"01234567-89ab-4cde-8fdc-ba9876543210"};

// same, but byte 8 puts it in the "future" variant, i.e. what a byte-swapped
// GUID typically looks like
constexpr std::string_view kBadVariantUuidStr{
    "01234567-89ab-4cde-ffdc-ba9876543210"};

constexpr std::string_view kNilUuidStr{"00000000-0000-0000-0000-000000000000"};

digest_bytes make_digest(std::uint8_t seed) {
  digest_bytes d{};
  for (std::size_t i = 0; i < d.size(); ++i) {
    d[i] = static_cast<std::uint8_t>(seed + i + 1);
  }
  return d;
}

std::string as_chars(std::span<std::uint8_t const> bytes) {
  return std::string(reinterpret_cast<char const*>(bytes.data()), bytes.size());
}

testing::AssertionResult
digest_eq(std::span<std::uint8_t const> actual, digest_bytes const& expected) {
  if (std::ranges::equal(actual, expected)) {
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure()
         << "digest mismatch (" << actual.size() << " bytes)";
}

template <typename T>
void put_le(std::string& buf, std::size_t offset, T value) {
  static_assert(std::is_unsigned_v<T>);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    buf[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
  }
}

void put_bytes(std::string& buf, std::size_t offset, std::string_view data) {
  std::ranges::copy(data, buf.begin() + offset);
}

// Recompute both hashes over an otherwise complete superblock. This is an
// independent implementation of what superblock_editor does.
std::string with_checksums(std::string sb) {
  std::array<std::uint8_t, 8> xxh3{};
  std::array<std::uint8_t, 32> sha2{};

  {
    checksum cs(checksum::xxh3_64);
    cs.update(sb.data() + kOffNumber, sb.size() - kOffNumber);
    EXPECT_EQ(sizeof(xxh3), cs.digest_size());
    EXPECT_TRUE(cs.finalize(xxh3.data()));
  }

  std::memcpy(sb.data() + kOffXxh3, xxh3.data(), xxh3.size());

  {
    checksum cs(checksum::sha2_512_256);
    cs.update(sb.data() + kOffXxh3, sb.size() - kOffXxh3);
    EXPECT_EQ(sizeof(sha2), cs.digest_size());
    EXPECT_TRUE(cs.finalize(sha2.data()));
  }

  std::memcpy(sb.data() + kOffSha2, sha2.data(), sha2.size());

  return sb;
}

class superblock_builder {
 public:
  superblock_builder()
      : sb_(kSbSize, '\0') {
    put_bytes(sb_, kOffMagic, "DWARFS"sv);
    sb_[kOffMajor] = static_cast<char>(MAJOR_VERSION);
    sb_[kOffMinor] = static_cast<char>(MINOR_VERSION);
    put_le<std::uint32_t>(sb_, kOffNumber, 0);
    put_le<std::uint16_t>(sb_, kOffType,
                          std::to_underlying(section_type::SUPERBLOCK));
    put_le<std::uint16_t>(sb_, kOffCompression,
                          std::to_underlying(compression_type::NONE));
    put_le<std::uint64_t>(sb_, kOffLength, kPayloadSize);
    sb_[kOffSbMajor] = static_cast<char>(SUPERBLOCK_MAJOR_VERSION);
    sb_[kOffSbMinor] = static_cast<char>(SUPERBLOCK_MINOR_VERSION);
    sb_[kOffAlignLog2] = static_cast<char>(kAlignLog2);
  }

  superblock_builder& section_minor_version(std::uint8_t v) {
    sb_[kOffMinor] = static_cast<char>(v);
    return *this;
  }

  superblock_builder& sb_major_version(std::uint8_t v) {
    sb_[kOffSbMajor] = static_cast<char>(v);
    return *this;
  }

  superblock_builder& sb_minor_version(std::uint8_t v) {
    sb_[kOffSbMinor] = static_cast<char>(v);
    return *this;
  }

  superblock_builder& align_log2(std::uint8_t v) {
    sb_[kOffAlignLog2] = static_cast<char>(v);
    return *this;
  }

  superblock_builder& fs_size(std::uint64_t v) {
    put_le<std::uint64_t>(sb_, kOffFsSize, v);
    return *this;
  }

  superblock_builder& uuid(uuid_bytes const& u) {
    std::memcpy(sb_.data() + kOffUuid, u.data(), u.size());
    return *this;
  }

  superblock_builder& label(std::string_view l) {
    EXPECT_LE(l.size(), kLabelSize);
    put_bytes(sb_, kOffLabel, l);
    return *this;
  }

  superblock_builder& digests(digest_algorithm algo, std::uint8_t scheme) {
    sb_[kOffDigestAlgo] = static_cast<char>(std::to_underlying(algo));
    sb_[kOffDigestScheme] = static_cast<char>(scheme);
    return *this;
  }

  superblock_builder& attr_digest(digest_bytes const& d) {
    std::memcpy(sb_.data() + kOffAttrDigest, d.data(), d.size());
    return *this;
  }

  superblock_builder& tree_digest(digest_bytes const& d) {
    std::memcpy(sb_.data() + kOffTreeDigest, d.data(), d.size());
    return *this;
  }

  superblock_builder& reserved1(std::string_view data) {
    EXPECT_LE(data.size(), kReserved1Size);
    put_bytes(sb_, kOffReserved1, data);
    return *this;
  }

  superblock_builder& reserved2(std::string_view data) {
    EXPECT_LE(data.size(), kReserved2Size);
    put_bytes(sb_, kOffReserved2, data);
    return *this;
  }

  /// Payload bytes beyond sizeof(superblock_v1), i.e. what a future minor
  /// version that grew the structure would produce.
  superblock_builder& extra(std::string_view data) {
    sb_.resize(kSbSize);
    sb_.append(data);
    put_le<std::uint64_t>(sb_, kOffLength, kPayloadSize + data.size());
    return *this;
  }

  /// Fully initialized digests, so that check_superblock() is satisfied.
  superblock_builder& with_digests(std::uint8_t attr_seed = 1) {
    return digests(kAlgo, kScheme).attr_digest(make_digest(attr_seed));
  }

  std::string& raw() { return sb_; }

  std::string build() const { return with_checksums(sb_); }

 private:
  std::string sb_;
};

std::string filler(std::size_t size, std::uint32_t seed) {
  std::string data(size, '\0');
  std::mt19937 rng{seed};
  for (auto& c : data) {
    c = static_cast<char>(rng() & 0xff);
  }
  return data;
}

std::vector<std::size_t> diff_offsets(std::string_view a, std::string_view b) {
  std::vector<std::size_t> diffs;
  EXPECT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    if (a[i] != b[i]) {
      diffs.push_back(i);
    }
  }
  return diffs;
}

bool in_any_range(
    std::size_t offset,
    std::vector<std::pair<std::size_t, std::size_t>> const& ranges) {
  return std::ranges::any_of(ranges, [offset](auto const& r) {
    return offset >= r.first && offset < r.first + r.second;
  });
}

// A complete image in memory, along with an editor that has read the
// superblock at `offset`.
class test_image {
 public:
  explicit test_image(std::string image, std::size_t offset = 0)
      : original_{std::move(image)}
      , io_{original_, kBinInOut}
      , offset_{offset} {
    io_.seekg(static_cast<std::streamoff>(offset_));
    ed_.read(io_);
  }

  superblock_editor& editor() { return ed_; }

  std::stringstream& stream() { return io_; }

  std::string const& original() const { return original_; }

  std::string current() { return io_.str(); }

  std::string superblock() {
    return current().substr(offset_, ed_.section_size());
  }

  void update() { ed_.update(io_); }

  // Simulate someone else modifying the image behind our back.
  void poke(std::size_t offset, std::string_view data) {
    io_.seekp(static_cast<std::streamoff>(offset));
    io_.write(data.data(), static_cast<std::streamsize>(data.size()));
    io_.flush();
    ASSERT_TRUE(io_.good());
  }

 private:
  std::string original_;
  std::stringstream io_;
  std::size_t offset_;
  superblock_editor ed_;
};

std::string read_label(std::string const& image, std::size_t offset = 0) {
  std::istringstream is{image, kBinIn};
  is.seekg(static_cast<std::streamoff>(offset));
  superblock_editor ed;
  ed.read(is);
  return std::string{ed.fs_label()};
}

// A stream buffer that cannot report a position, i.e. a pipe.
class unseekable_streambuf : public std::streambuf {
 public:
  explicit unseekable_streambuf(std::string data)
      : data_{std::move(data)} {
    setg(data_.data(), data_.data(), data_.data() + data_.size());
  }

 private:
  pos_type seekoff(off_type, std::ios::seekdir, std::ios::openmode) override {
    return pos_type(off_type(-1));
  }

  pos_type seekpos(pos_type, std::ios::openmode) override {
    return pos_type(off_type(-1));
  }

  std::string data_;
};

// A real string buffer that reports every put operation, so that tests can
// pin down exactly how much data update() writes, and can make writing fail.
class put_tracking_stringbuf : public std::stringbuf {
 public:
  explicit put_tracking_stringbuf(std::string const& data)
      : std::stringbuf{data, kBinInOut} {}

  MOCK_METHOD(bool, on_put, (std::streamsize n), ());
  MOCK_METHOD(void, on_overflow, (), ());

  std::streamsize xsputn(char const* s, std::streamsize n) override {
    if (!on_put(n)) {
      return 0; // simulate a failing device
    }
    return std::stringbuf::xsputn(s, n);
  }

  int_type overflow(int_type c) override {
    on_overflow();
    return std::stringbuf::overflow(c);
  }
};

} // namespace

//
// Reading
//

TEST(superblock_editor_test, read_accepts_a_valid_superblock) {
  auto const attr = make_digest(1);
  auto const tree = make_digest(100);

  test_image img{superblock_builder{}
                     .fs_size(16 * kAlignment)
                     .label("my-label")
                     .uuid(kUuid)
                     .digests(kAlgo, kScheme)
                     .attr_digest(attr)
                     .tree_digest(tree)
                     .build()};
  auto& ed = img.editor();

  EXPECT_EQ(0, ed.image_offset());
  EXPECT_EQ(kSbSize, ed.section_size());
  EXPECT_EQ(SUPERBLOCK_MAJOR_VERSION, ed.major_version());
  EXPECT_EQ(SUPERBLOCK_MINOR_VERSION, ed.minor_version());
  EXPECT_EQ(kAlignment, ed.fs_size_alignment());
  ASSERT_TRUE(ed.fs_size().has_value());
  EXPECT_EQ(16 * kAlignment, ed.fs_size().value());
  EXPECT_EQ("my-label", ed.fs_label());
  ASSERT_TRUE(ed.fs_uuid().has_value());
  EXPECT_EQ(kUuidStr, ed.fs_uuid().value());
  EXPECT_EQ(kAlgo, ed.digest_algo());
  EXPECT_EQ(kScheme, ed.digest_scheme_version());
  EXPECT_TRUE(digest_eq(ed.attr_digest(), attr));
  EXPECT_TRUE(digest_eq(ed.tree_digest(), tree));
}

TEST(superblock_editor_test, uninitialized_fields_read_as_absent) {
  test_image img{superblock_builder{}.build()};
  auto& ed = img.editor();

  EXPECT_FALSE(ed.fs_size().has_value());
  EXPECT_FALSE(ed.fs_uuid().has_value());
  EXPECT_TRUE(ed.fs_label().empty());
  EXPECT_EQ(digest_algorithm::UNINITIALIZED, ed.digest_algo());
  EXPECT_EQ(0, ed.digest_scheme_version());
  EXPECT_TRUE(ed.attr_digest().empty());
  EXPECT_TRUE(ed.tree_digest().empty());
}

TEST(superblock_editor_test, attribute_digest_without_a_tree_digest) {
  auto const attr = make_digest(7);
  test_image img{
      superblock_builder{}.digests(kAlgo, kScheme).attr_digest(attr).build()};

  EXPECT_TRUE(digest_eq(img.editor().attr_digest(), attr));
  EXPECT_TRUE(img.editor().tree_digest().empty());
}

TEST(superblock_editor_test, read_accepts_accepted_section_minor_versions) {
  for (std::uint8_t minor = MINOR_VERSION; minor <= MINOR_VERSION_ACCEPTED;
       ++minor) {
    auto const sb = superblock_builder{}.section_minor_version(minor).build();
    std::istringstream is{sb, kBinIn};
    superblock_editor ed;
    EXPECT_NO_THROW(ed.read(is)) << "section minor version " << int(minor);
  }
}

TEST(superblock_editor_test, read_reports_the_image_offset) {
  auto const header = filler(1337, 7);
  test_image img{header + superblock_builder{}.build(), header.size()};

  EXPECT_EQ(static_cast<std::streamoff>(header.size()),
            img.editor().image_offset());
}

TEST(superblock_editor_test, read_rejects_short_input) {
  auto const sb = superblock_builder{}.build();

  for (std::size_t size :
       {std::size_t{0}, std::size_t{1}, kHdrSize, kSbSize - 1}) {
    std::istringstream is{sb.substr(0, size), kBinIn};
    superblock_editor ed;
    EXPECT_THROW(ed.read(is), std::runtime_error) << "size " << size;
  }
}

TEST(superblock_editor_test, read_rejects_an_invalid_section_header) {
  struct testcase {
    char const* what;
    std::function<void(superblock_builder&)> corrupt;
  };

  std::vector<testcase> const cases{
      {"magic", [](auto& b) { b.raw()[kOffMagic + 3] = 'X'; }},
      {"major version",
       [](auto& b) {
         b.raw()[kOffMajor] = static_cast<char>(MAJOR_VERSION + 1);
       }},
      {"minor version too small",
       [](auto& b) { b.section_minor_version(MINOR_VERSION - 1); }},
      {"minor version too large",
       [](auto& b) { b.section_minor_version(MINOR_VERSION_ACCEPTED + 1); }},
      {"section number",
       [](auto& b) { put_le<std::uint32_t>(b.raw(), kOffNumber, 1); }},
      {"section type",
       [](auto& b) {
         put_le<std::uint16_t>(b.raw(), kOffType,
                               std::to_underlying(section_type::BLOCK));
       }},
      {"compression",
       [](auto& b) {
         put_le<std::uint16_t>(b.raw(), kOffCompression,
                               std::to_underlying(compression_type::NONE) + 1);
       }},
      {"length too small",
       [](auto& b) {
         put_le<std::uint64_t>(b.raw(), kOffLength, kPayloadSize - 1);
       }},
      {"length absurdly large",
       [](auto& b) { put_le<std::uint64_t>(b.raw(), kOffLength, 1ULL << 40); }},
  };

  for (auto const& tc : cases) {
    superblock_builder builder;
    tc.corrupt(builder);
    // The hashes are recomputed *after* corrupting, so these exercise the
    // header checks and not the integrity checks.
    std::istringstream is{builder.build(), kBinIn};
    superblock_editor ed;
    EXPECT_THROW(ed.read(is), std::runtime_error) << "case: " << tc.what;
  }
}

TEST(superblock_editor_test, read_rejects_an_invalid_superblock) {
  struct testcase {
    char const* what;
    std::function<void(superblock_builder&)> corrupt;
  };

  std::vector<testcase> const cases{
      // major version 0 is the obsolete draft of this structure
      {"obsolete major version", [](auto& b) { b.sb_major_version(0); }},
      {"unknown major version",
       [](auto& b) { b.sb_major_version(SUPERBLOCK_MAJOR_VERSION + 1); }},
      {"zero minor version", [](auto& b) { b.sb_minor_version(0); }},
      {"alignment out of range", [](auto& b) { b.align_log2(64); }},
      {"unterminated label",
       [](auto& b) { b.label(std::string(kLabelSize, 'x')); }},
      {"digests without an algorithm",
       [](auto& b) { b.attr_digest(make_digest(1)); }},
      {"scheme without an algorithm",
       [](auto& b) { b.raw()[kOffDigestScheme] = 1; }},
      {"algorithm without a scheme",
       [](auto& b) { b.digests(kAlgo, 0).attr_digest(make_digest(1)); }},
      {"algorithm without an attribute digest",
       [](auto& b) { b.digests(kAlgo, kScheme); }},
      {"tree digest without an attribute digest",
       [](auto& b) { b.digests(kAlgo, kScheme).tree_digest(make_digest(1)); }},
  };

  for (auto const& tc : cases) {
    superblock_builder builder;
    tc.corrupt(builder);
    std::istringstream is{builder.build(), kBinIn};
    superblock_editor ed;
    EXPECT_THROW(ed.read(is), std::runtime_error) << "case: " << tc.what;
  }
}

TEST(superblock_editor_test, read_accepts_a_label_filling_the_field) {
  std::string const label(kMaxLabelLength, 'x');
  test_image img{superblock_builder{}.label(label).build()};

  EXPECT_EQ(label, img.editor().fs_label());
}

TEST(superblock_editor_test, read_rejects_broken_hashes) {
  for (std::size_t offset : {kOffXxh3, kOffSha2}) {
    auto sb = superblock_builder{}.build();
    sb[offset] ^= 0x01;
    std::istringstream is{sb, kBinIn};
    superblock_editor ed;
    EXPECT_THROW(ed.read(is), std::runtime_error) << "offset " << offset;
  }
}

// The magic and the section header version are the only bytes not covered
// by the hashes, and both are checked exactly, so nothing can slip through.
TEST(superblock_editor_test, read_detects_every_single_bit_flip) {
  auto const sb = superblock_builder{}
                      .fs_size(1234 * kAlignment)
                      .label("label")
                      .uuid(kUuid)
                      .with_digests()
                      .build();

  for (std::size_t byte = 0; byte < sb.size(); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      auto corrupted = sb;
      corrupted[byte] = static_cast<char>(corrupted[byte] ^ (1 << bit));

      std::istringstream is{corrupted, kBinIn};
      superblock_editor ed;
      EXPECT_THROW(ed.read(is), std::runtime_error)
          << "undetected corruption at byte " << byte << ", bit " << bit;
    }
  }
}

TEST(superblock_editor_test, read_twice_throws) {
  auto const sb = superblock_builder{}.build();
  std::istringstream is{sb + sb, kBinIn};

  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  EXPECT_THROW(ed.read(is), std::runtime_error);
}

TEST(superblock_editor_test, read_reports_a_failing_stream) {
  std::istringstream is{superblock_builder{}.build(), kBinIn};
  is.setstate(std::ios::badbit);

  superblock_editor ed;

  EXPECT_THROW(ed.read(is), std::runtime_error);
}

TEST(superblock_editor_test, accessors_throw_before_read) {
  superblock_editor ed;
  auto const digest = make_digest(1);

  EXPECT_THROW(ed.image_offset(), std::runtime_error);
  EXPECT_THROW(ed.section_size(), std::runtime_error);
  EXPECT_THROW(ed.major_version(), std::runtime_error);
  EXPECT_THROW(ed.minor_version(), std::runtime_error);
  EXPECT_THROW(ed.digest_algo(), std::runtime_error);
  EXPECT_THROW(ed.digest_scheme_version(), std::runtime_error);
  EXPECT_THROW(ed.fs_size_alignment(), std::runtime_error);
  EXPECT_THROW(ed.fs_size(), std::runtime_error);
  EXPECT_THROW(ed.fs_uuid(), std::runtime_error);
  EXPECT_THROW(ed.fs_label(), std::runtime_error);
  EXPECT_THROW(ed.attr_digest(), std::runtime_error);
  EXPECT_THROW(ed.tree_digest(), std::runtime_error);
  EXPECT_THROW(ed.init_fs_size(kAlignment), std::runtime_error);
  EXPECT_THROW(ed.set_fs_uuid("random"), std::runtime_error);
  EXPECT_THROW(ed.set_fs_label("nope"), std::runtime_error);
  EXPECT_THROW(ed.set_digests(kAlgo, kScheme, digest), std::runtime_error);
  EXPECT_THROW(ed.set_tree_digest(digest), std::runtime_error);
  EXPECT_THROW(ed.clear_digests(), std::runtime_error);
}

// A superblock that did not pass validation must never be retained, so that
// a later update() cannot possibly push it into an image.
TEST(superblock_editor_test, failed_read_leaves_the_editor_untouched) {
  auto bad = superblock_builder{}.build();
  bad[kOffLabel] = 'x'; // invalidates both hashes

  superblock_editor ed;

  {
    std::istringstream is{bad, kBinIn};
    ASSERT_THROW(ed.read(is), std::runtime_error);
  }

  EXPECT_THROW(ed.fs_label(), std::runtime_error);

  {
    auto const good = superblock_builder{}.label("good").build();
    std::stringstream io{good, kBinInOut};
    EXPECT_THROW(ed.update(io), std::runtime_error);
    EXPECT_EQ(good, io.str()) << "update() wrote after a failed read()";
  }

  // ... and the editor must still be usable
  auto const good = superblock_builder{}.label("good").build();
  std::istringstream is{good, kBinIn};
  ASSERT_NO_THROW(ed.read(is));
  EXPECT_EQ("good", ed.fs_label());
}

TEST(superblock_editor_test,
     failed_read_of_truncated_input_leaves_editor_untouched) {
  auto const sb = superblock_builder{}.build();

  superblock_editor ed;

  {
    std::istringstream is{sb.substr(0, kSbSize - 8), kBinIn};
    ASSERT_THROW(ed.read(is), std::runtime_error);
  }

  std::istringstream is{sb, kBinIn};
  EXPECT_NO_THROW(ed.read(is));
}

//
// Forward compatibility
//
// The editor only interprets fields whose meaning is fixed for all minor
// versions. Everything else must be accepted as-is and preserved verbatim.
//

TEST(superblock_editor_test, read_accepts_a_newer_superblock_minor_version) {
  test_image img{superblock_builder{}
                     .sb_minor_version(SUPERBLOCK_MINOR_VERSION + 4)
                     .label("future")
                     .build()};

  EXPECT_EQ(SUPERBLOCK_MINOR_VERSION + 4, img.editor().minor_version());
  EXPECT_EQ("future", img.editor().fs_label());
}

TEST(superblock_editor_test, read_accepts_a_longer_superblock) {
  auto const extra = filler(64, 21);
  test_image img{superblock_builder{}
                     .sb_minor_version(SUPERBLOCK_MINOR_VERSION + 1)
                     .label("grown")
                     .extra(extra)
                     .build()};

  EXPECT_EQ(kSbSize + extra.size(), img.editor().section_size());
  EXPECT_EQ("grown", img.editor().fs_label());
}

TEST(superblock_editor_test, update_preserves_unknown_fields) {
  auto const res1 = filler(kReserved1Size, 41);
  auto const res2 = filler(kReserved2Size, 42);
  auto const extra = filler(48, 43);

  test_image img{superblock_builder{}
                     .sb_minor_version(SUPERBLOCK_MINOR_VERSION + 1)
                     .reserved1(res1)
                     .reserved2(res2)
                     .extra(extra)
                     .build()};

  img.editor().set_fs_label("whatever");
  img.update();

  auto const updated = img.superblock();

  EXPECT_EQ(res1, updated.substr(kOffReserved1, kReserved1Size));
  EXPECT_EQ(res2, updated.substr(kOffReserved2, kReserved2Size));
  EXPECT_EQ(extra, updated.substr(kSbSize, extra.size()));
  // an edit must never lower the minor version ...
  EXPECT_EQ(SUPERBLOCK_MINOR_VERSION + 1, img.editor().minor_version());
  EXPECT_EQ(static_cast<char>(SUPERBLOCK_MINOR_VERSION + 1),
            updated[kOffSbMinor]);
}

// ... and must not raise it either, unless a field introduced by a later
// minor version was actually written.
TEST(superblock_editor_test, update_does_not_bump_the_minor_version) {
  test_image img{superblock_builder{}.label("before").build()};

  img.editor().set_fs_label("after");
  img.update();

  EXPECT_EQ(SUPERBLOCK_MINOR_VERSION, img.editor().minor_version());
  EXPECT_EQ(static_cast<char>(SUPERBLOCK_MINOR_VERSION),
            img.superblock()[kOffSbMinor]);
}

// An unknown algorithm means the digests are present but uninterpretable:
// how many of the stored bytes are significant follows from the algorithm.
// They must read as empty, but survive an unrelated edit untouched.
TEST(superblock_editor_test, an_unknown_digest_algorithm_is_preserved) {
  auto const attr = make_digest(8);
  test_image img{superblock_builder{}
                     .digests(kUnknownAlgo, kScheme)
                     .attr_digest(attr)
                     .build()};

  EXPECT_EQ(kUnknownAlgo, img.editor().digest_algo());
  EXPECT_FALSE(is_known_digest_algorithm(img.editor().digest_algo()));
  EXPECT_TRUE(img.editor().attr_digest().empty());
  EXPECT_TRUE(img.editor().tree_digest().empty());

  img.editor().set_fs_label("unrelated edit");
  img.update();

  EXPECT_EQ(as_chars(attr),
            img.superblock().substr(kOffAttrDigest, kDigestSize));
}

TEST(superblock_editor_test,
     a_tree_digest_cannot_be_added_under_an_unknown_algorithm) {
  test_image img{superblock_builder{}
                     .digests(kUnknownAlgo, kScheme)
                     .attr_digest(make_digest(8))
                     .build()};

  EXPECT_THROW(img.editor().set_tree_digest(make_digest(9)),
               std::runtime_error);
}

//
// Updating
//

TEST(superblock_editor_test, update_without_modification_is_byte_identical) {
  test_image img{superblock_builder{}
                     .fs_size(42 * kAlignment)
                     .label("some label")
                     .uuid(kUuid)
                     .with_digests()
                     .build()};

  img.update();

  EXPECT_EQ(img.original(), img.current());
}

TEST(superblock_editor_test, update_is_idempotent) {
  test_image img{superblock_builder{}.build()};

  img.editor().init_fs_size(7 * kAlignment);
  img.editor().set_fs_uuid(superblock_editor::kUuidRandom);
  img.editor().set_fs_label("label");
  img.editor().set_digests(kAlgo, kScheme, make_digest(3), make_digest(9));

  ASSERT_NO_THROW(img.update());
  auto const first = img.current();

  ASSERT_NO_THROW(img.update());
  EXPECT_EQ(first, img.current());
}

TEST(superblock_editor_test, update_throws_before_read) {
  auto const sb = superblock_builder{}.build();
  std::stringstream io{sb, kBinInOut};

  superblock_editor ed;

  EXPECT_THROW(ed.update(io), std::runtime_error);
  EXPECT_EQ(sb, io.str());
}

TEST(superblock_editor_test, updated_superblock_can_be_read_back) {
  test_image img{superblock_builder{}.build()};

  auto const attr = make_digest(11);
  auto const tree = make_digest(77);

  img.editor().init_fs_size(123 * kAlignment);
  img.editor().set_fs_uuid(kUuidStr);
  img.editor().set_fs_label("round trip");
  img.editor().set_digests(kAlgo, kScheme, attr, tree);

  img.update();

  auto const sb = img.superblock();

  // the hashes must match what an independent implementation computes
  EXPECT_EQ(with_checksums(sb), sb);

  std::istringstream is{sb, kBinIn};
  superblock_editor reread;
  ASSERT_NO_THROW(reread.read(is));
  EXPECT_EQ(123 * kAlignment, reread.fs_size().value());
  EXPECT_EQ(kUuidStr, reread.fs_uuid().value());
  EXPECT_EQ("round trip", reread.fs_label());
  EXPECT_EQ(kAlgo, reread.digest_algo());
  EXPECT_EQ(kScheme, reread.digest_scheme_version());
  EXPECT_TRUE(digest_eq(reread.attr_digest(), attr));
  EXPECT_TRUE(digest_eq(reread.tree_digest(), tree));
}

TEST(superblock_editor_test, update_only_modifies_the_expected_fields) {
  test_image img{superblock_builder{}.label("aaaa").build()};

  img.editor().init_fs_size(2 * kAlignment);
  img.editor().set_fs_uuid(kUuidStr);
  img.editor().set_fs_label("bbbbbb");
  img.editor().set_digests(kAlgo, kScheme, make_digest(5));

  img.update();

  for (auto offset : diff_offsets(img.original(), img.current())) {
    EXPECT_TRUE(in_any_range(offset, {{kOffSha2, 32},
                                      {kOffXxh3, 8},
                                      {kOffDigestAlgo, 2},
                                      {kOffFsSize, 8},
                                      {kOffUuid, kUuidSize},
                                      {kOffLabel, kLabelSize},
                                      {kOffAttrDigest, kDigestSize}}))
        << "unexpected modification at offset " << offset;
  }
}

// Updating must never touch a single byte outside of the superblock section.
TEST(superblock_editor_test, update_does_not_touch_the_rest_of_the_image) {
  auto const header = filler(1337, 1);
  constexpr std::size_t kImageSize{16 * kAlignment};
  auto const sections = superblock_builder{}.label("before").build() +
                        filler(kImageSize - kSbSize, 2);

  test_image img{header + sections, header.size()};

  img.editor().init_fs_size(kImageSize);
  img.editor().set_fs_uuid(superblock_editor::kUuidRandom);
  img.editor().set_fs_label("after");

  ASSERT_NO_THROW(img.update());

  auto const updated = img.current();

  ASSERT_EQ(img.original().size(), updated.size()) << "image size changed";
  EXPECT_EQ(img.original().substr(0, header.size()),
            updated.substr(0, header.size()))
      << "image header was modified";
  EXPECT_EQ(img.original().substr(header.size() + kSbSize),
            updated.substr(header.size() + kSbSize))
      << "image data after the superblock was modified";

  EXPECT_EQ("after", read_label(updated, header.size()));
}

TEST(superblock_editor_test,
     update_rejects_a_concurrently_modified_superblock) {
  test_image img{superblock_builder{}.label("original").build()};

  img.editor().set_fs_label("ours");

  // someone else updates the superblock in the meantime
  auto const meddled = superblock_builder{}.label("theirs").build();
  img.poke(0, meddled);

  EXPECT_THROW(img.update(), std::runtime_error);
  EXPECT_EQ(meddled, img.current()) << "update() clobbered a foreign change";
}

TEST(superblock_editor_test, update_rejects_a_corrupted_superblock) {
  test_image img{superblock_builder{}.label("original").build()};

  img.editor().set_fs_label("ours");

  auto corrupted = img.original();
  corrupted[kOffLabel + 1] ^= 0x40; // breaks both hashes
  img.poke(0, corrupted);

  EXPECT_THROW(img.update(), std::runtime_error);
  EXPECT_EQ(corrupted, img.current())
      << "update() wrote to an image it could not validate";
}

TEST(superblock_editor_test, update_rejects_a_foreign_stream) {
  auto const sb = superblock_builder{}.label("original").build();

  std::istringstream is{sb, kBinIn};
  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  ed.set_fs_label("ours");

  // an image that is not the one we read from
  for (auto const& image : {filler(kSbSize, 5), sb.substr(0, kSbSize - 8),
                            superblock_builder{}.label("elsewhere").build()}) {
    std::stringstream io{image, kBinInOut};
    EXPECT_THROW(ed.update(io), std::runtime_error);
    EXPECT_EQ(image, io.str());
  }
}

TEST(superblock_editor_test, update_rejects_a_wrong_offset) {
  auto const header = filler(1024, 9);
  auto const sb = superblock_builder{}.build();

  // read at the correct offset ...
  std::istringstream is{header + sb, kBinIn};
  is.seekg(static_cast<std::streamoff>(header.size()));
  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  ed.set_fs_label("ours");

  // ... but hand it an image where the superblock is somewhere else, so
  // that the offset we would write to holds unrelated data
  auto const image = filler(512, 10) + sb + filler(2048, 11);
  ASSERT_GT(image.size(), header.size() + kSbSize);
  std::stringstream io{image, kBinInOut};

  EXPECT_THROW(ed.update(io), std::runtime_error);
  EXPECT_EQ(image, io.str());
}

TEST(superblock_editor_test, update_requires_a_seekable_read) {
  auto const sb = superblock_builder{}.build();

  unseekable_streambuf buf{sb};
  std::istream is{&buf};

  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  EXPECT_LT(ed.image_offset(), 0);
  EXPECT_EQ("", ed.fs_label());

  std::stringstream io{sb, kBinInOut};
  EXPECT_THROW(ed.update(io), std::runtime_error);
  EXPECT_EQ(sb, io.str());
}

TEST(superblock_editor_test, update_puts_exactly_one_superblock) {
  auto const sb = superblock_builder{}.build();

  std::istringstream is{sb, kBinIn};
  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  ed.set_fs_label("changed");

  NiceMock<put_tracking_stringbuf> buf{sb};

  // exactly one put of exactly the section size, and no character-wise
  // writes; anything else is an unexpected call and fails the test
  EXPECT_CALL(buf, on_put(static_cast<std::streamsize>(kSbSize)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(buf, on_overflow()).Times(0);

  std::iostream io{&buf};

  EXPECT_NO_THROW(ed.update(io));
  EXPECT_EQ("changed", read_label(buf.str()));
}

TEST(superblock_editor_test, update_reports_a_failing_stream) {
  auto const sb = superblock_builder{}.build();

  std::istringstream is{sb, kBinIn};
  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));
  ed.set_fs_label("changed");

  NiceMock<put_tracking_stringbuf> buf{sb};

  EXPECT_CALL(buf, on_put(_)).Times(1).WillOnce(Return(false));

  std::iostream io{&buf};

  EXPECT_THROW(ed.update(io), std::runtime_error);
  EXPECT_EQ(sb, buf.str());
}

TEST(superblock_editor_test, update_reports_a_stream_that_is_already_bad) {
  auto const sb = superblock_builder{}.build();

  std::istringstream is{sb, kBinIn};
  superblock_editor ed;
  ASSERT_NO_THROW(ed.read(is));

  std::stringstream io{sb, kBinInOut};
  io.setstate(std::ios::badbit);

  EXPECT_THROW(ed.update(io), std::runtime_error);
  EXPECT_EQ(sb, io.str());
}

//
// fs_size (write-once)
//

TEST(superblock_editor_test, init_fs_size_sets_the_size) {
  test_image img{superblock_builder{}.build()};

  ASSERT_FALSE(img.editor().fs_size().has_value());
  img.editor().init_fs_size(5 * kAlignment);
  ASSERT_TRUE(img.editor().fs_size().has_value());
  EXPECT_EQ(5 * kAlignment, img.editor().fs_size().value());
}

TEST(superblock_editor_test, init_fs_size_twice_throws) {
  test_image img{superblock_builder{}.build()};

  ASSERT_NO_THROW(img.editor().init_fs_size(5 * kAlignment));
  EXPECT_THROW(img.editor().init_fs_size(6 * kAlignment), std::runtime_error);
  EXPECT_EQ(5 * kAlignment, img.editor().fs_size().value());
}

TEST(superblock_editor_test, init_fs_size_throws_if_already_set_in_the_image) {
  test_image img{superblock_builder{}.fs_size(5 * kAlignment).build()};

  EXPECT_THROW(img.editor().init_fs_size(6 * kAlignment), std::runtime_error);
}

TEST(superblock_editor_test, init_fs_size_rejects_invalid_sizes) {
  for (std::uint64_t size :
       {std::uint64_t{0}, std::uint64_t{128}, kAlignment + 1, kAlignment - 1}) {
    test_image img{superblock_builder{}.build()};
    EXPECT_THROW(img.editor().init_fs_size(size), std::runtime_error)
        << "size " << size;
    EXPECT_FALSE(img.editor().fs_size().has_value()) << "size " << size;
    // a rejected size must not have left a trace
    img.update();
    EXPECT_EQ(img.original(), img.current()) << "size " << size;
  }
}

TEST(superblock_editor_test, init_fs_size_with_an_alignment_of_one) {
  // a log2 alignment of zero means no alignment at all
  test_image img{superblock_builder{}.align_log2(0).build()};

  EXPECT_EQ(1, img.editor().fs_size_alignment());
  ASSERT_NO_THROW(img.editor().init_fs_size(1000003));
  EXPECT_EQ(1000003, img.editor().fs_size().value());
  EXPECT_NO_THROW(img.update());
}

TEST(superblock_editor_test, init_fs_size_accounts_for_a_longer_superblock) {
  auto const extra = filler(4096, 31);
  test_image img{superblock_builder{}.align_log2(0).extra(extra).build()};

  // anything smaller than the section itself cannot be a valid image size
  EXPECT_THROW(img.editor().init_fs_size(kSbSize), std::runtime_error);
  EXPECT_NO_THROW(img.editor().init_fs_size(kSbSize + extra.size()));
}

//
// UUID
//

TEST(superblock_editor_test, set_fs_uuid_generates_a_random_uuid) {
  test_image img{superblock_builder{}.build()};

  ASSERT_FALSE(img.editor().fs_uuid().has_value());
  img.editor().set_fs_uuid(superblock_editor::kUuidRandom);
  ASSERT_TRUE(img.editor().fs_uuid().has_value());
  EXPECT_EQ(36, img.editor().fs_uuid().value().size());
  EXPECT_NE(kNilUuidStr, img.editor().fs_uuid().value());
  // boost's random generator produces version 4 UUIDs
  EXPECT_EQ('4', img.editor().fs_uuid().value()[14]);
}

TEST(superblock_editor_test, random_uuids_are_distinct) {
  auto const sb = superblock_builder{}.build();

  test_image a{sb};
  test_image b{sb};

  a.editor().set_fs_uuid(superblock_editor::kUuidRandom);
  b.editor().set_fs_uuid(superblock_editor::kUuidRandom);

  EXPECT_NE(a.editor().fs_uuid().value(), b.editor().fs_uuid().value());
}

TEST(superblock_editor_test, set_fs_uuid_roundtrips_through_the_image) {
  test_image img{superblock_builder{}.build()};

  img.editor().set_fs_uuid(kUuidStr);
  EXPECT_EQ(kUuidStr, img.editor().fs_uuid().value());

  img.update();

  EXPECT_EQ(as_chars(kUuid), img.superblock().substr(kOffUuid, kUuidSize));
}

TEST(superblock_editor_test, set_fs_uuid_replaces_an_existing_uuid) {
  constexpr std::string_view other{"aabbccdd-eeff-4a0b-b102-030405060708"};

  test_image img{superblock_builder{}.uuid(kUuid).build()};

  ASSERT_NO_THROW(img.editor().set_fs_uuid(other));
  EXPECT_EQ(other, img.editor().fs_uuid().value());
}

TEST(superblock_editor_test, nil_removes_the_uuid) {
  for (auto const nil : {superblock_editor::kUuidNil, kNilUuidStr}) {
    test_image img{superblock_builder{}.uuid(kUuid).build()};

    ASSERT_NO_THROW(img.editor().set_fs_uuid(nil)) << "spelling: " << nil;
    EXPECT_FALSE(img.editor().fs_uuid().has_value()) << "spelling: " << nil;

    img.update();

    EXPECT_EQ(std::string(kUuidSize, '\0'),
              img.superblock().substr(kOffUuid, kUuidSize));
  }
}

TEST(superblock_editor_test, set_fs_uuid_rejects_invalid_input) {
  for (auto const uuid :
       {""sv, "not-a-uuid"sv, "RANDOM"sv, "Nil"sv,
        "01234567-89ab-4cde-8fdc-ba98765432"sv, kBadVariantUuidStr}) {
    test_image img{superblock_builder{}.uuid(kUuid).build()};

    EXPECT_THROW(img.editor().set_fs_uuid(uuid), std::runtime_error)
        << "uuid: " << uuid;
    // a rejected UUID must leave the previous one intact
    EXPECT_EQ(kUuidStr, img.editor().fs_uuid().value()) << "uuid: " << uuid;

    img.update();
    EXPECT_EQ(img.original(), img.current()) << "uuid: " << uuid;
  }
}

//
// Label
//

TEST(superblock_editor_test, set_fs_label_roundtrip) {
  for (auto label :
       {""sv, "x"sv, "a slightly longer label"sv, "ümlaut and 日本語"sv,
        "012345678901234567890123456789012345678901234567890123456789012"sv}) {
    test_image img{superblock_builder{}.build()};
    ASSERT_NO_THROW(img.editor().set_fs_label(label)) << "label " << label;
    EXPECT_EQ(label, img.editor().fs_label());

    img.update();
    EXPECT_EQ(label, read_label(img.current()));
  }
}

TEST(superblock_editor_test, set_fs_label_zero_pads_the_remainder) {
  test_image img{superblock_builder{}.label("a very long label").build()};

  img.editor().set_fs_label("short");
  EXPECT_EQ("short", img.editor().fs_label());

  img.update();

  EXPECT_EQ(std::string("short") + std::string(kLabelSize - 5, '\0'),
            img.superblock().substr(kOffLabel, kLabelSize));
}

TEST(superblock_editor_test, an_empty_label_removes_it) {
  test_image img{superblock_builder{}.label("goodbye").build()};

  img.editor().set_fs_label("");
  EXPECT_TRUE(img.editor().fs_label().empty());

  img.update();

  EXPECT_EQ(std::string(kLabelSize, '\0'),
            img.superblock().substr(kOffLabel, kLabelSize));
}

TEST(superblock_editor_test, set_fs_label_rejects_invalid_labels) {
  struct testcase {
    char const* what;
    std::string label;
  };

  std::vector<testcase> const cases{
      {"too long", std::string(kMaxLabelLength + 1, 'x')},
      {"embedded null", std::string{"foo\0bar"sv}},
      {"truncated utf-8", "caf\xc3"},
      {"invalid utf-8", "\xff\xfe"},
      {"overlong encoding", "\xc0\xaf"},
      {"surrogate", "\xed\xa0\x80"},
  };

  for (auto const& tc : cases) {
    test_image img{superblock_builder{}.label("keep me").build()};

    EXPECT_THROW(img.editor().set_fs_label(tc.label), std::runtime_error)
        << "case: " << tc.what;
    EXPECT_EQ("keep me", img.editor().fs_label()) << "case: " << tc.what;

    img.update();
    EXPECT_EQ(img.original(), img.current()) << "case: " << tc.what;
  }
}

//
// Digests
//

TEST(superblock_editor_test, set_digests_with_attributes_only) {
  auto const attr = make_digest(2);
  test_image img{superblock_builder{}.build()};

  img.editor().set_digests(kAlgo, kScheme, attr);

  EXPECT_EQ(kAlgo, img.editor().digest_algo());
  EXPECT_EQ(kScheme, img.editor().digest_scheme_version());
  EXPECT_TRUE(digest_eq(img.editor().attr_digest(), attr));
  EXPECT_TRUE(img.editor().tree_digest().empty());

  img.update();

  EXPECT_EQ(as_chars(attr),
            img.superblock().substr(kOffAttrDigest, kDigestSize));
  EXPECT_EQ(std::string(kDigestSize, '\0'),
            img.superblock().substr(kOffTreeDigest, kDigestSize));
}

TEST(superblock_editor_test, set_digests_with_both_digests) {
  auto const attr = make_digest(2);
  auto const tree = make_digest(200);
  test_image img{superblock_builder{}.build()};

  img.editor().set_digests(kAlgo, kScheme, attr, tree);

  EXPECT_TRUE(digest_eq(img.editor().attr_digest(), attr));
  EXPECT_TRUE(digest_eq(img.editor().tree_digest(), tree));
}

TEST(superblock_editor_test, set_digests_replaces_an_existing_pair) {
  test_image img{superblock_builder{}
                     .digests(kAlgo, kScheme)
                     .attr_digest(make_digest(4))
                     .tree_digest(make_digest(44))
                     .build()};

  auto const attr = make_digest(6);
  auto const tree = make_digest(66);

  img.editor().set_digests(kAlgo, kScheme + 1, attr, tree);

  EXPECT_EQ(kScheme + 1, img.editor().digest_scheme_version());
  EXPECT_TRUE(digest_eq(img.editor().attr_digest(), attr));
  EXPECT_TRUE(digest_eq(img.editor().tree_digest(), tree));
}

// A tree digest computed under a different scheme is meaningless, so
// replacing the pair without one must clear it rather than leave it behind.
TEST(superblock_editor_test, set_digests_without_a_tree_digest_clears_it) {
  test_image img{superblock_builder{}
                     .digests(kAlgo, kScheme)
                     .attr_digest(make_digest(4))
                     .tree_digest(make_digest(44))
                     .build()};

  img.editor().set_digests(kAlgo, kScheme + 1, make_digest(6));

  EXPECT_TRUE(img.editor().tree_digest().empty());

  img.update();

  EXPECT_EQ(std::string(kDigestSize, '\0'),
            img.superblock().substr(kOffTreeDigest, kDigestSize));
}

TEST(superblock_editor_test, set_digests_rejects_invalid_arguments) {
  auto const attr = make_digest(2);
  digest_bytes const zero{};
  std::array<std::uint8_t, kDigestSize / 2> const too_short{1, 2, 3, 4};

  struct testcase {
    char const* what;
    std::function<void(superblock_editor&)> apply;
  };

  std::vector<testcase> const cases{
      {"uninitialized algorithm",
       [&](auto& ed) {
         ed.set_digests(digest_algorithm::UNINITIALIZED, kScheme, attr);
       }},
      {"unknown algorithm",
       [&](auto& ed) { ed.set_digests(kUnknownAlgo, kScheme, attr); }},
      {"zero scheme version",
       [&](auto& ed) { ed.set_digests(kAlgo, 0, attr); }},
      {"all-zero attribute digest",
       [&](auto& ed) { ed.set_digests(kAlgo, kScheme, zero); }},
      {"all-zero tree digest",
       [&](auto& ed) { ed.set_digests(kAlgo, kScheme, attr, zero); }},
      {"short attribute digest",
       [&](auto& ed) { ed.set_digests(kAlgo, kScheme, too_short); }},
      {"short tree digest",
       [&](auto& ed) { ed.set_digests(kAlgo, kScheme, attr, too_short); }},
  };

  for (auto const& tc : cases) {
    test_image img{superblock_builder{}.build()};
    EXPECT_THROW(tc.apply(img.editor()), std::runtime_error)
        << "case: " << tc.what;
    EXPECT_EQ(digest_algorithm::UNINITIALIZED, img.editor().digest_algo())
        << "case: " << tc.what;

    img.update();
    EXPECT_EQ(img.original(), img.current()) << "case: " << tc.what;
  }
}

TEST(superblock_editor_test, set_tree_digest_completes_an_existing_pair) {
  auto const attr = make_digest(4);
  auto const tree = make_digest(44);

  test_image img{
      superblock_builder{}.digests(kAlgo, kScheme).attr_digest(attr).build()};

  ASSERT_TRUE(img.editor().tree_digest().empty());
  img.editor().set_tree_digest(tree);
  EXPECT_TRUE(digest_eq(img.editor().tree_digest(), tree));
  EXPECT_TRUE(digest_eq(img.editor().attr_digest(), attr));

  img.update();

  EXPECT_EQ(as_chars(tree),
            img.superblock().substr(kOffTreeDigest, kDigestSize));
}

TEST(superblock_editor_test, set_tree_digest_replaces_an_existing_one) {
  test_image img{superblock_builder{}
                     .digests(kAlgo, kScheme)
                     .attr_digest(make_digest(4))
                     .tree_digest(make_digest(44))
                     .build()};

  auto const tree = make_digest(55);
  img.editor().set_tree_digest(tree);
  EXPECT_TRUE(digest_eq(img.editor().tree_digest(), tree));
}

TEST(superblock_editor_test, set_tree_digest_without_attributes_throws) {
  test_image img{superblock_builder{}.build()};

  EXPECT_THROW(img.editor().set_tree_digest(make_digest(1)),
               std::runtime_error);
  EXPECT_TRUE(img.editor().tree_digest().empty());
}

TEST(superblock_editor_test, clear_digests) {
  test_image img{superblock_builder{}
                     .digests(kAlgo, kScheme)
                     .attr_digest(make_digest(4))
                     .tree_digest(make_digest(44))
                     .build()};

  img.editor().clear_digests();

  EXPECT_EQ(digest_algorithm::UNINITIALIZED, img.editor().digest_algo());
  EXPECT_EQ(0, img.editor().digest_scheme_version());
  EXPECT_TRUE(img.editor().attr_digest().empty());
  EXPECT_TRUE(img.editor().tree_digest().empty());

  img.update();

  auto const sb = img.superblock();
  EXPECT_EQ(std::string(2, '\0'), sb.substr(kOffDigestAlgo, 2));
  EXPECT_EQ(std::string(2 * kDigestSize, '\0'),
            sb.substr(kOffAttrDigest, 2 * kDigestSize));
}
