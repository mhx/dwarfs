/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <istream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <dwarfs/checksum.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/superblock_editor.h>

namespace dwarfs {

namespace {

constexpr std::string_view kMagic{"DWARFS"};

// The SUPERBLOCK section was introduced with file system version 2.5.
constexpr std::uint8_t kMinMinorVersion{5};

struct superblock_section {
  section_header_v2 hdr;
  superblock_v0 sb;
};

// The whole implementation reads and writes `superblock_section` objects
// as raw bytes and expresses the checksum ranges as offsets into the
// object representation, so pin down the layout. If any of these fire,
// the code below must be changed to (de)serialize field by field.
static_assert(std::is_trivially_copyable_v<superblock_section>);
static_assert(std::is_standard_layout_v<superblock_section>);
static_assert(std::has_unique_object_representations_v<superblock_section>);
static_assert(sizeof(superblock_section) ==
              superblock_editor::superblock_size());
static_assert(offsetof(superblock_section, sb) == sizeof(section_header_v2));

// The XXH3-64 covers everything starting at `number`, the SHA2-512/256
// covers everything starting at `xxh3_64`.
constexpr std::size_t kXxhStart{offsetof(superblock_section, hdr.number)};
constexpr std::size_t kShaStart{offsetof(superblock_section, hdr.xxh3_64)};

std::span<std::byte const>
covered_range(superblock_section const& s, std::size_t start) {
  return std::span<std::byte const>{reinterpret_cast<std::byte const*>(&s),
                                    sizeof(s)}
      .subspan(start);
}

// Note that only the section header is checked here. The contents of the
// superblock itself are deliberately *not* validated: the fields we know
// about are valid in any superblock version, and any field we don't know
// about is none of our business. This keeps the editor usable on images
// written by future versions, whose contents it preserves verbatim.
void check_section_header(section_header_v2 const& hdr) {
  if (std::string_view{hdr.magic.data(), hdr.magic.size()} != kMagic) {
    throw std::runtime_error("invalid superblock magic");
  }

  // Rejecting minor versions we don't know about is not only required by
  // the format's compatibility rules, it also means that *every* single
  // bit flip anywhere in the superblock is detected: the magic and the
  // version are the only fields not covered by the hashes.
  if (hdr.major != MAJOR_VERSION || hdr.minor < kMinMinorVersion ||
      hdr.minor > MINOR_VERSION_ACCEPTED) {
    throw std::runtime_error("invalid superblock version");
  }

  if (hdr.number != 0) {
    throw std::runtime_error("invalid superblock section number");
  }

  if (hdr.type != std::to_underlying(section_type::SUPERBLOCK)) {
    throw std::runtime_error("invalid superblock section type");
  }

  if (hdr.compression != std::to_underlying(compression_type::NONE)) {
    throw std::runtime_error("invalid superblock compression type");
  }

  if (hdr.length != sizeof(superblock_v0)) {
    throw std::runtime_error("invalid superblock size");
  }
}

void update_checksums(superblock_section& s) {
  // Order matters: the SHA2 range includes the XXH3 field.
  {
    checksum cs(checksum::xxh3_64);
    cs.update(covered_range(s, kXxhStart));
    assert(cs.digest_size() == sizeof(s.hdr.xxh3_64));
    if (!cs.finalize(&s.hdr.xxh3_64)) {
      throw std::runtime_error("failed to compute superblock XXH3-64 checksum");
    }
  }

  {
    checksum cs(checksum::sha2_512_256);
    cs.update(covered_range(s, kShaStart));
    assert(cs.digest_size() == sizeof(s.hdr.sha2_512_256));
    if (!cs.finalize(s.hdr.sha2_512_256.data())) {
      throw std::runtime_error(
          "failed to compute superblock SHA2-512/256 checksum");
    }
  }
}

void verify_checksums(superblock_section const& s) {
  auto const xxh3 = covered_range(s, kXxhStart);
  if (!checksum::verify(checksum::xxh3_64, xxh3.data(), xxh3.size(),
                        &s.hdr.xxh3_64, sizeof(s.hdr.xxh3_64))) {
    throw std::runtime_error("superblock XXH3-64 checksum verification failed");
  }

  auto const sha2 = covered_range(s, kShaStart);
  if (!checksum::verify(checksum::sha2_512_256, sha2.data(), sha2.size(),
                        s.hdr.sha2_512_256.data(), s.hdr.sha2_512_256.size())) {
    throw std::runtime_error(
        "superblock SHA2-512/256 checksum verification failed");
  }
}

/// Read a superblock section from `input` and fully validate it. Used both
/// when initially reading the superblock and when re-checking the section
/// an update is about to overwrite.
superblock_section parse(std::istream& input) {
  superblock_section s;

  input.read(reinterpret_cast<char*>(&s), sizeof(s));

  if (!input || input.gcount() != static_cast<std::streamsize>(sizeof(s))) {
    throw std::runtime_error("failed to read superblock");
  }

  check_section_header(s.hdr);
  verify_checksums(s);

  return s;
}

bool identical(superblock_section const& a, superblock_section const& b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

class superblock_editor::impl {
 public:
  void read(std::istream& input);
  void update(std::iostream& io);

  std::streamoff image_offset() const {
    assert_loaded();
    return offset_;
  }

  std::uint32_t fs_size_alignment() const { return sb().fs_size_alignment; }

  std::optional<std::uint64_t> fs_size() const {
    std::optional<std::uint64_t> size;
    if (std::uint64_t const sz = sb().fs_size; sz != 0) {
      size = sz;
    }
    return size;
  }

  std::optional<std::string> fs_uuid() const {
    auto const uuid = load_uuid();
    if (uuid.is_nil()) {
      return std::nullopt;
    }
    return boost::uuids::to_string(uuid);
  }

  std::string_view fs_label() const {
    auto const& raw = sb().fs_label;
    std::string_view label{raw.data(), raw.size()};
    if (auto const end = label.find('\0'); end != std::string_view::npos) {
      label.remove_suffix(label.size() - end);
    }
    return label;
  }

  void init_fs_size(std::uint64_t fs_size) {
    auto& sblk = sb();

    if (sblk.fs_size != 0) {
      throw std::runtime_error("filesystem size has already been set");
    }

    // A size of zero is how "not set yet" is represented, and the size
    // must at least cover the superblock section itself.
    if (fs_size < sizeof(superblock_section)) {
      throw std::runtime_error("filesystem size is too small");
    }

    std::uint32_t const align = sblk.fs_size_alignment;

    if (align == 0) {
      throw std::runtime_error("invalid filesystem size alignment");
    }

    if (fs_size % align != 0) {
      throw std::runtime_error(
          "filesystem size is not a multiple of the alignment");
    }

    sblk.fs_size = fs_size;
  }

  void init_fs_uuid() {
    static_assert(sizeof(boost::uuids::uuid) == kUuidSize);
    static_assert(std::tuple_size_v<decltype(superblock_v0::fs_uuid)> ==
                  kUuidSize);
    auto const uuid = boost::uuids::random_generator()();
    init_fs_uuid(
        std::span<std::uint8_t const, kUuidSize>{uuid.begin(), kUuidSize});
  }

  void init_fs_uuid(std::span<std::uint8_t const, kUuidSize> uuid) {
    if (!load_uuid().is_nil()) {
      throw std::runtime_error("filesystem UUID has already been set");
    }

    if (std::ranges::all_of(uuid, [](auto b) { return b == 0; })) {
      throw std::runtime_error("refusing to set a nil filesystem UUID");
    }

    std::ranges::copy(uuid, sb().fs_uuid.begin());
  }

  void set_fs_label(std::string_view label) {
    auto& raw = sb().fs_label;

    // Check everything before touching the superblock, so a rejected
    // label leaves the previous one intact.
    if (label.size() > raw.size()) {
      throw std::runtime_error("filesystem label is too long");
    }

    if (label.find('\0') != std::string_view::npos) {
      throw std::runtime_error(
          "filesystem label must not contain null characters");
    }

    auto const end = std::ranges::copy(label, raw.begin()).out;
    std::fill(end, raw.end(), '\0');
  }

 private:
  boost::uuids::uuid load_uuid() const {
    boost::uuids::uuid uuid;
    std::ranges::copy(sb().fs_uuid, uuid.begin());
    return uuid;
  }

  superblock_v0& sb() {
    assert_loaded();
    return s_->sb;
  }

  superblock_v0 const& sb() const {
    assert_loaded();
    return s_->sb;
  }

  void assert_loaded() const {
    if (!s_) {
      throw std::runtime_error("superblock has not been read");
    }
  }

  // The superblock being edited, ...
  std::optional<superblock_section> s_;
  // ... and the one currently stored in the image.
  std::optional<superblock_section> image_;
  std::streamoff offset_{-1};
};

void superblock_editor::impl::read(std::istream& input) {
  if (s_) {
    throw std::runtime_error("superblock has already been read");
  }

  auto const offset = input.tellg();

  // Only commit once everything has been validated. Otherwise a failed
  // read would leave unvalidated data in the editor, which a subsequent
  // update() could push into an image.
  auto const s = parse(input);

  s_ = s;
  image_ = s;
  offset_ = offset < 0 ? -1 : static_cast<std::streamoff>(offset);
}

void superblock_editor::impl::update(std::iostream& io) {
  assert_loaded();

  if (offset_ < 0) {
    throw std::runtime_error("superblock was not read from a seekable stream");
  }

  // Build the new section first. Re-running the section header checks
  // means a bug in one of the setters cannot turn a valid image into an
  // invalid one.
  auto s = *s_;

  check_section_header(s.hdr);
  update_checksums(s);
  verify_checksums(s);

  // Make sure the section we're about to overwrite is still a valid
  // superblock and still exactly the one we read. This catches both a
  // stream that doesn't refer to the image we read from and concurrent
  // modifications, either of which we'd otherwise silently destroy.
  io.seekg(offset_);

  if (!io) {
    throw std::runtime_error("failed to seek to superblock offset");
  }

  if (!identical(parse(io), *image_)) {
    throw std::runtime_error(
        "superblock in the image has changed since it was read");
  }

  io.seekp(offset_);

  if (!io) {
    throw std::runtime_error("failed to seek to superblock offset");
  }

  io.write(reinterpret_cast<char const*>(&s), sizeof(s));
  io.flush();

  if (!io) {
    throw std::runtime_error("failed to write superblock");
  }

  if (auto const end = io.tellp();
      end < 0 || end - offset_ != static_cast<std::streamoff>(sizeof(s))) {
    throw std::runtime_error(
        "unexpected output position after writing superblock");
  }

  image_ = s;
}

superblock_editor::superblock_editor()
    : impl_{std::make_unique<impl>()} {}

superblock_editor::~superblock_editor() = default;

void superblock_editor::read(std::istream& input) { impl_->read(input); }

void superblock_editor::update(std::iostream& io) { impl_->update(io); }

std::streamoff superblock_editor::image_offset() const {
  return impl_->image_offset();
}

std::uint32_t superblock_editor::fs_size_alignment() const {
  return impl_->fs_size_alignment();
}

std::optional<std::uint64_t> superblock_editor::fs_size() const {
  return impl_->fs_size();
}

std::optional<std::string> superblock_editor::fs_uuid() const {
  return impl_->fs_uuid();
}

std::string_view superblock_editor::fs_label() const {
  return impl_->fs_label();
}

void superblock_editor::init_fs_size(std::uint64_t fs_size) {
  impl_->init_fs_size(fs_size);
}

void superblock_editor::init_fs_uuid() { impl_->init_fs_uuid(); }

void superblock_editor::init_fs_uuid(
    std::span<std::uint8_t const, kUuidSize> uuid) {
  impl_->init_fs_uuid(uuid);
}

void superblock_editor::set_fs_label(std::string_view label) {
  impl_->set_fs_label(label);
}

} // namespace dwarfs
