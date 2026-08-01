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
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/checksum.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/superblock_editor.h>

using namespace dwarfs;
using namespace std::string_view_literals;

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace {

// The tests deliberately build superblocks byte by byte instead of reusing
// the structs from fstypes.h. That way they also pin down the on-disk
// layout and are independent of both the host's endianness and of any
// future change to those structs.

constexpr std::size_t kSbSize{superblock_editor::superblock_size()};
constexpr std::size_t kHdrSize{64};

constexpr std::size_t kOffMagic{0};
constexpr std::size_t kOffMajor{6};
constexpr std::size_t kOffMinor{7};
constexpr std::size_t kOffSha2{8};
constexpr std::size_t kOffXxh3{40};
constexpr std::size_t kOffNumber{48};
constexpr std::size_t kOffType{52};
constexpr std::size_t kOffCompression{54};
constexpr std::size_t kOffLength{56};
constexpr std::size_t kOffSbVersion{64};
constexpr std::size_t kOffReserved0{66};
constexpr std::size_t kOffAlignment{68};
constexpr std::size_t kOffFsSize{72};
constexpr std::size_t kOffUuid{80};
constexpr std::size_t kOffLabel{96};
constexpr std::size_t kOffReserved1{160};

constexpr std::size_t kUuidSize{16};
constexpr std::size_t kLabelSize{64};
constexpr std::size_t kReserved1Size{96};

constexpr std::uint32_t kAlignment{4096};

constexpr auto kBinIn{std::ios::in | std::ios::binary};
constexpr auto kBinInOut{std::ios::in | std::ios::out | std::ios::binary};

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
    put_le<std::uint64_t>(sb_, kOffLength, kSbSize - kHdrSize);
    put_le<std::uint16_t>(sb_, kOffSbVersion, SUPERBLOCK_VERSION);
    put_le<std::uint32_t>(sb_, kOffAlignment, kAlignment);
  }

  superblock_builder& minor_version(std::uint8_t v) {
    sb_[kOffMinor] = static_cast<char>(v);
    return *this;
  }

  superblock_builder& superblock_version(std::uint16_t v) {
    put_le<std::uint16_t>(sb_, kOffSbVersion, v);
    return *this;
  }

  superblock_builder& alignment(std::uint32_t v) {
    put_le<std::uint32_t>(sb_, kOffAlignment, v);
    return *this;
  }

  superblock_builder& fs_size(std::uint64_t v) {
    put_le<std::uint64_t>(sb_, kOffFsSize, v);
    return *this;
  }

  superblock_builder& uuid(std::array<std::uint8_t, kUuidSize> const& u) {
    std::memcpy(sb_.data() + kOffUuid, u.data(), u.size());
    return *this;
  }

  superblock_builder& label(std::string_view l) {
    EXPECT_LE(l.size(), kLabelSize);
    put_bytes(sb_, kOffLabel, l);
    return *this;
  }

  superblock_builder& reserved0(std::uint16_t v) {
    put_le<std::uint16_t>(sb_, kOffReserved0, v);
    return *this;
  }

  superblock_builder& reserved1(std::string_view data) {
    EXPECT_LE(data.size(), kReserved1Size);
    put_bytes(sb_, kOffReserved1, data);
    return *this;
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

  std::string superblock() { return current().substr(offset_, kSbSize); }

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

TEST(superblock_editor_test, read_accepts_a_valid_superblock) {
  test_image img{
      superblock_builder{}
          .fs_size(16 * kAlignment)
          .label("my-label")
          .uuid({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16})
          .build()};
  auto& ed = img.editor();

  EXPECT_EQ(0, ed.image_offset());
  EXPECT_EQ(kAlignment, ed.fs_size_alignment());
  ASSERT_TRUE(ed.fs_size().has_value());
  EXPECT_EQ(16 * kAlignment, ed.fs_size().value());
  EXPECT_EQ("my-label", ed.fs_label());
  ASSERT_TRUE(ed.fs_uuid().has_value());
  EXPECT_EQ("01020304-0506-0708-090a-0b0c0d0e0f10", ed.fs_uuid().value());
}

TEST(superblock_editor_test, read_accepts_accepted_minor_versions) {
  for (std::uint8_t minor = MINOR_VERSION; minor <= MINOR_VERSION_ACCEPTED;
       ++minor) {
    auto const sb = superblock_builder{}.minor_version(minor).build();
    std::istringstream is{sb, kBinIn};
    superblock_editor ed;
    EXPECT_NO_THROW(ed.read(is)) << "minor version " << int(minor);
  }
}

TEST(superblock_editor_test, uninitialized_size_and_uuid_read_as_nullopt) {
  test_image img{superblock_builder{}.build()};

  EXPECT_FALSE(img.editor().fs_size().has_value());
  EXPECT_FALSE(img.editor().fs_uuid().has_value());
  EXPECT_TRUE(img.editor().fs_label().empty());
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
       [](auto& b) { b.minor_version(MINOR_VERSION - 1); }},
      {"minor version too large",
       [](auto& b) { b.minor_version(MINOR_VERSION_ACCEPTED + 1); }},
      {"section number",
       [](auto& b) { put_le<std::uint32_t>(b.raw(), kOffNumber, 1); }},
      {"section type",
       [](auto& b) {
         put_le<std::uint16_t>(b.raw(), kOffType,
                               std::to_underlying(section_type::BLOCK));
       }},
      {"compression",
       [](auto& b) {
         // anything but NONE
         put_le<std::uint16_t>(b.raw(), kOffCompression,
                               std::to_underlying(compression_type::NONE) + 1);
       }},
      {"section length",
       [](auto& b) { put_le<std::uint64_t>(b.raw(), kOffLength, 128); }},
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

TEST(superblock_editor_test, read_rejects_broken_hashes) {
  for (std::size_t offset : {kOffXxh3, kOffSha2}) {
    auto sb = superblock_builder{}.build();
    sb[offset] ^= 0x01;
    std::istringstream is{sb, kBinIn};
    superblock_editor ed;
    EXPECT_THROW(ed.read(is), std::runtime_error) << "offset " << offset;
  }
}

TEST(superblock_editor_test, read_detects_every_single_bit_flip) {
  auto const sb =
      superblock_builder{}.fs_size(1234 * kAlignment).label("label").build();

  for (std::size_t byte = 0; byte < kSbSize; ++byte) {
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

  EXPECT_THROW(ed.image_offset(), std::runtime_error);
  EXPECT_THROW(ed.fs_size_alignment(), std::runtime_error);
  EXPECT_THROW(ed.fs_size(), std::runtime_error);
  EXPECT_THROW(ed.fs_uuid(), std::runtime_error);
  EXPECT_THROW(ed.fs_label(), std::runtime_error);
  EXPECT_THROW(ed.init_fs_size(kAlignment), std::runtime_error);
  EXPECT_THROW(ed.init_fs_uuid(), std::runtime_error);
  EXPECT_THROW(ed.set_fs_label("nope"), std::runtime_error);
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

// Forward Compatibility
//
// The editor only interprets fields whose meaning is fixed for all
// superblock versions. Everything else must be accepted as-is and
// preserved verbatim.
TEST(superblock_editor_test, read_accepts_an_unknown_superblock_version) {
  test_image img{superblock_builder{}
                     .superblock_version(SUPERBLOCK_VERSION + 1)
                     .label("future")
                     .build()};

  EXPECT_EQ("future", img.editor().fs_label());
}

TEST(superblock_editor_test, read_accepts_used_reserved_fields) {
  test_image img{superblock_builder{}
                     .superblock_version(SUPERBLOCK_VERSION + 1)
                     .reserved0(0xbeef)
                     .reserved1(filler(kReserved1Size, 3))
                     .build()};

  EXPECT_NO_THROW(img.editor().set_fs_label("still works"));
}

TEST(superblock_editor_test, read_does_not_interpret_the_size) {
  // no alignment checks on read: whatever is in the image is what we report
  test_image img{superblock_builder{}
                     .alignment(kAlignment)
                     .fs_size(kAlignment + 1)
                     .build()};

  EXPECT_EQ(kAlignment + 1, img.editor().fs_size().value());
}

TEST(superblock_editor_test, update_preserves_unknown_fields) {
  auto const reserved = filler(kReserved1Size, 42);
  test_image img{superblock_builder{}
                     .superblock_version(SUPERBLOCK_VERSION + 1)
                     .reserved0(0xbeef)
                     .reserved1(reserved)
                     .build()};

  img.editor().set_fs_label("whatever");
  img.update();

  auto const updated = img.superblock();

  EXPECT_EQ(img.original().substr(kOffSbVersion, 2),
            updated.substr(kOffSbVersion, 2));
  EXPECT_EQ(img.original().substr(kOffReserved0, 2),
            updated.substr(kOffReserved0, 2));
  EXPECT_EQ(reserved, updated.substr(kOffReserved1, kReserved1Size));
}

TEST(superblock_editor_test, update_without_modification_is_byte_identical) {
  test_image img{
      superblock_builder{}
          .fs_size(42 * kAlignment)
          .label("some label")
          .uuid({0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12})
          .build()};

  img.update();

  EXPECT_EQ(img.original(), img.current());
}

TEST(superblock_editor_test, update_is_idempotent) {
  test_image img{superblock_builder{}.build()};

  img.editor().init_fs_size(7 * kAlignment);
  img.editor().init_fs_uuid();
  img.editor().set_fs_label("label");

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

  img.editor().init_fs_size(123 * kAlignment);
  img.editor().init_fs_uuid();
  img.editor().set_fs_label("round trip");

  img.update();

  auto const sb = img.superblock();

  // the hashes must match what an independent implementation computes
  EXPECT_EQ(with_checksums(sb), sb);

  std::istringstream is{sb, kBinIn};
  superblock_editor reread;
  ASSERT_NO_THROW(reread.read(is));
  EXPECT_EQ(123 * kAlignment, reread.fs_size().value());
  EXPECT_EQ(img.editor().fs_uuid(), reread.fs_uuid());
  EXPECT_EQ("round trip", reread.fs_label());
}

TEST(superblock_editor_test, update_only_modifies_the_expected_fields) {
  test_image img{superblock_builder{}.label("aaaa").build()};

  img.editor().init_fs_size(2 * kAlignment);
  img.editor().init_fs_uuid();
  img.editor().set_fs_label("bbbbbb");

  img.update();

  for (auto offset : diff_offsets(img.original(), img.current())) {
    EXPECT_TRUE(in_any_range(offset, {{kOffSha2, 32},
                                      {kOffXxh3, 8},
                                      {kOffFsSize, 8},
                                      {kOffUuid, kUuidSize},
                                      {kOffLabel, kLabelSize}}))
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
  img.editor().init_fs_uuid();
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

  // exactly one put of exactly 256 bytes, and no character-wise writes
  // anything else is an unexpected call and fails the test
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
       {std::uint64_t{0}, std::uint64_t{128}, std::uint64_t{kAlignment + 1},
        std::uint64_t{kAlignment - 1}}) {
    test_image img{superblock_builder{}.build()};
    EXPECT_THROW(img.editor().init_fs_size(size), std::runtime_error)
        << "size " << size;
    EXPECT_FALSE(img.editor().fs_size().has_value()) << "size " << size;
    // a rejected size must not have left a trace
    img.update();
    EXPECT_EQ(img.original(), img.current()) << "size " << size;
  }
}

TEST(superblock_editor_test, init_fs_size_rejects_an_invalid_alignment) {
  // an alignment of 0 is not legal, "no alignment" is expressed as 1
  test_image img{superblock_builder{}.alignment(0).build()};

  EXPECT_THROW(img.editor().init_fs_size(1000000), std::runtime_error);
}

TEST(superblock_editor_test, init_fs_size_with_an_alignment_of_one) {
  test_image img{superblock_builder{}.alignment(1).build()};

  ASSERT_NO_THROW(img.editor().init_fs_size(1000003));
  EXPECT_EQ(1000003, img.editor().fs_size().value());
  EXPECT_NO_THROW(img.update());
}

TEST(superblock_editor_test, init_fs_uuid_generates_a_non_nil_uuid) {
  test_image img{superblock_builder{}.build()};

  ASSERT_FALSE(img.editor().fs_uuid().has_value());
  img.editor().init_fs_uuid();
  ASSERT_TRUE(img.editor().fs_uuid().has_value());
  EXPECT_EQ(36, img.editor().fs_uuid().value().size());
  EXPECT_NE("00000000-0000-0000-0000-000000000000",
            img.editor().fs_uuid().value());
}

TEST(superblock_editor_test, init_fs_uuid_generates_distinct_uuids) {
  auto const sb = superblock_builder{}.build();

  test_image a{sb};
  test_image b{sb};

  a.editor().init_fs_uuid();
  b.editor().init_fs_uuid();

  EXPECT_NE(a.editor().fs_uuid().value(), b.editor().fs_uuid().value());
}

TEST(superblock_editor_test, init_fs_uuid_twice_throws) {
  test_image img{superblock_builder{}.build()};

  ASSERT_NO_THROW(img.editor().init_fs_uuid());
  auto const uuid = img.editor().fs_uuid();
  EXPECT_THROW(img.editor().init_fs_uuid(), std::runtime_error);
  EXPECT_EQ(uuid, img.editor().fs_uuid());
}

TEST(superblock_editor_test, init_fs_uuid_throws_if_already_set_in_the_image) {
  test_image img{
      superblock_builder{}
          .uuid({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16})
          .build()};

  EXPECT_THROW(img.editor().init_fs_uuid(), std::runtime_error);
}

TEST(superblock_editor_test, init_fs_uuid_with_explicit_value) {
  std::array<std::uint8_t, kUuidSize> const uuid{
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

  test_image img{superblock_builder{}.build()};
  img.editor().init_fs_uuid(uuid);

  EXPECT_EQ("01234567-89ab-cdef-fedc-ba9876543210",
            img.editor().fs_uuid().value());

  img.update();

  EXPECT_EQ(
      std::string(reinterpret_cast<char const*>(uuid.data()), uuid.size()),
      img.superblock().substr(kOffUuid, kUuidSize));
}

TEST(superblock_editor_test, init_fs_uuid_rejects_a_nil_uuid) {
  std::array<std::uint8_t, kUuidSize> const nil{};

  test_image img{superblock_builder{}.build()};

  EXPECT_THROW(img.editor().init_fs_uuid(nil), std::runtime_error);
  EXPECT_FALSE(img.editor().fs_uuid().has_value());
}

TEST(superblock_editor_test, set_fs_label_roundtrip) {
  for (auto label :
       {""sv, "x"sv, "a slightly longer label"sv,
        "0123456789012345678901234567890123456789012345678901234567890123"sv}) {
    test_image img{superblock_builder{}.build()};
    ASSERT_NO_THROW(img.editor().set_fs_label(label));
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

TEST(superblock_editor_test, set_fs_label_rejects_a_too_long_label) {
  test_image img{superblock_builder{}.label("keep me").build()};

  EXPECT_THROW(img.editor().set_fs_label(std::string(kLabelSize + 1, 'x')),
               std::runtime_error);
  EXPECT_EQ("keep me", img.editor().fs_label());

  img.update();
  EXPECT_EQ(img.original(), img.current());
}

TEST(superblock_editor_test, set_fs_label_rejects_embedded_null_characters) {
  test_image img{superblock_builder{}.label("keep me").build()};

  EXPECT_THROW(img.editor().set_fs_label("foo\0bar"sv), std::runtime_error);
  EXPECT_EQ("keep me", img.editor().fs_label());

  img.update();
  EXPECT_EQ(img.original(), img.current());
}

TEST(superblock_editor_test, label_filling_the_whole_field_is_not_truncated) {
  std::string const label(kLabelSize, 'x');
  test_image img{superblock_builder{}.label(label).build()};

  EXPECT_EQ(label, img.editor().fs_label());
}
