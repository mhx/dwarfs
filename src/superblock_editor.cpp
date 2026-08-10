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
#include <cstddef>
#include <cstring>
#include <istream>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <dwarfs/checksum.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/superblock.h>
#include <dwarfs/superblock_editor.h>

namespace dwarfs {

namespace {

constexpr std::string_view kMagic{"DWARFS"};

// The SUPERBLOCK section was introduced with file system version 2.5.
constexpr std::uint8_t kMinMinorVersion{5};

constexpr std::size_t kHeaderSize{sizeof(section_header_v2)};
constexpr std::size_t kKnownPayloadSize{sizeof(superblock_v1)};
constexpr std::size_t kMaxDigestSize{superblock_v1::kMaxDigestSize};

// A superblock is tiny by design. Any payload beyond this is either
// corruption or a format we have no business editing. The bound matters
// because `hdr.length` has to be trusted *before* the checksums can be
// verified: it determines how much data to read in the first place.
constexpr std::uint64_t kMaxPayloadSize{64 * 1024};

// `fs_size_align_log2` is corruption-controlled and ends up in a shift, so
// it needs a hard bound. Section index entries pack offsets into 48 bits,
// which makes anything beyond that meaningless anyway.
constexpr std::uint8_t kMaxSizeAlignLog2{48};

// Minor version in which each mutable field was introduced. `update()`
// writes the maximum of the minor version that was read and the minor
// version required by the fields that were actually modified, so editing an
// older superblock does not bump it unless a newer field is written.
constexpr std::uint8_t kMinorFsSize{1};
constexpr std::uint8_t kMinorFsUuid{1};
constexpr std::uint8_t kMinorFsLabel{1};
constexpr std::uint8_t kMinorDigests{1};

// The layout of both structures is pinned: the code below (de)serializes
// them as raw bytes and expresses the checksum ranges as byte offsets. If
// any of these fire, the code must be changed to (de)serialize field by
// field. The offsets within superblock_v1 are asserted in superblock.h.
static_assert(std::is_trivially_copyable_v<section_header_v2>);
static_assert(std::is_standard_layout_v<section_header_v2>);
static_assert(std::has_unique_object_representations_v<section_header_v2>);
static_assert(std::is_trivially_copyable_v<superblock_v1>);
static_assert(std::is_standard_layout_v<superblock_v1>);
static_assert(std::has_unique_object_representations_v<superblock_v1>);
static_assert(kHeaderSize + kKnownPayloadSize ==
              superblock_editor::min_section_size());

// The XXH3-64 covers everything starting at `number`, the SHA2-512/256
// covers everything starting at `xxh3_64`.
constexpr std::size_t kXxhStart{offsetof(section_header_v2, number)};
constexpr std::size_t kShaStart{offsetof(section_header_v2, xxh3_64)};
constexpr std::size_t kXxhOffset{offsetof(section_header_v2, xxh3_64)};
constexpr std::size_t kShaOffset{offsetof(section_header_v2, sha2_512_256)};
constexpr std::size_t kXxhSize{sizeof(section_header_v2::xxh3_64)};
constexpr std::size_t kShaSize{sizeof(section_header_v2::sha2_512_256)};

static_assert(kShaStart < kXxhStart);
static_assert(kShaOffset + kShaSize <= kShaStart);
static_assert(kXxhOffset + kXxhSize <= kXxhStart);

template <typename R>
bool all_zero(R const& range) {
  return std::ranges::all_of(range, [](auto b) { return b == 0; });
}

/// The in-memory representation of a superblock section: the header, the
/// part of the payload this version understands, and any trailing bytes
/// written by a future minor version, which are preserved verbatim.
struct superblock_section {
  section_header_v2 hdr{};
  superblock_v1 sb{};
  std::vector<std::byte> tail;

  std::size_t size() const {
    return kHeaderSize + kKnownPayloadSize + tail.size();
  }

  std::vector<std::byte> serialize() const {
    std::vector<std::byte> buf(size());
    std::memcpy(buf.data(), &hdr, kHeaderSize);
    std::memcpy(buf.data() + kHeaderSize, &sb, kKnownPayloadSize);
    if (!tail.empty()) {
      std::memcpy(buf.data() + kHeaderSize + kKnownPayloadSize, tail.data(),
                  tail.size());
    }
    return buf;
  }
};

struct parsed_superblock {
  superblock_section section;
  std::vector<std::byte> raw;
};

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

  // A superblock may be *longer* than the version we know about, but never
  // shorter: minor versions only ever add fields.
  if (hdr.length < kKnownPayloadSize) {
    throw std::runtime_error("superblock section is too small");
  }

  if (hdr.length > kMaxPayloadSize) {
    throw std::runtime_error("superblock section is too large");
  }
}

/// Only the fields that govern how the rest of the superblock is
/// interpreted are validated here, plus the invariants the format
/// guarantees. Everything else is none of our business: it keeps the editor
/// usable on images written by future minor versions, whose contents it
/// preserves verbatim.
///
/// Note that the label is checked for null termination but not for valid
/// UTF-8. A structurally broken field cannot be interpreted at all, whereas
/// a label with bad encoding is exactly the kind of thing someone would
/// reach for this editor to fix.
void check_superblock(superblock_v1 const& sb) {
  // Major version 0 was used by an early draft of this structure, which is
  // deliberately not supported.
  if (sb.major_version != SUPERBLOCK_MAJOR_VERSION) {
    throw std::runtime_error("unsupported superblock major version");
  }

  if (sb.minor_version == 0) {
    throw std::runtime_error("invalid superblock minor version");
  }

  if (sb.fs_size_align_log2 > kMaxSizeAlignLog2) {
    throw std::runtime_error("invalid filesystem size alignment");
  }

  if (sb.fs_label.back() != '\0') {
    throw std::runtime_error("filesystem label is not null-terminated");
  }

  if (sb.digest_algo == digest_algorithm::UNINITIALIZED) {
    if (sb.digest_scheme_version != 0 || !all_zero(sb.attr_digest) ||
        !all_zero(sb.tree_digest)) {
      throw std::runtime_error("filesystem digests without an algorithm");
    }
    return;
  }

  if (sb.digest_scheme_version == 0) {
    throw std::runtime_error("filesystem digests without a scheme version");
  }

  // Also catches a tree digest stored without an attribute digest.
  if (all_zero(sb.attr_digest)) {
    throw std::runtime_error("filesystem attribute digest is missing");
  }

  // A digest shorter than the field is stored left-aligned and zero-padded.
  // For an unknown algorithm there is nothing to check, since the
  // significant length is exactly what we don't know.
  if (auto const size = get_digest_algorithm_size(sb.digest_algo); size > 0) {
    if (size > kMaxDigestSize) {
      throw std::runtime_error("digest algorithm size exceeds field size");
    }
    if (!all_zero(std::span{sb.attr_digest}.subspan(size)) ||
        !all_zero(std::span{sb.tree_digest}.subspan(size))) {
      throw std::runtime_error("filesystem digest has non-zero padding");
    }
  }
}

void update_checksums(std::span<std::byte> buf) {
  assert(buf.size() >= kHeaderSize + kKnownPayloadSize);

  // Order matters: the SHA2 range includes the XXH3 field.
  {
    checksum cs(checksum::xxh3_64);
    cs.update(buf.subspan(kXxhStart));
    assert(cs.digest_size() == kXxhSize);
    if (!cs.finalize(buf.data() + kXxhOffset)) {
      throw std::runtime_error("failed to compute superblock XXH3-64 checksum");
    }
  }

  {
    checksum cs(checksum::sha2_512_256);
    cs.update(buf.subspan(kShaStart));
    assert(cs.digest_size() == kShaSize);
    if (!cs.finalize(buf.data() + kShaOffset)) {
      throw std::runtime_error(
          "failed to compute superblock SHA2-512/256 checksum");
    }
  }
}

void verify_checksums(std::span<std::byte const> buf) {
  assert(buf.size() >= kHeaderSize + kKnownPayloadSize);

  auto const xxh3 = buf.subspan(kXxhStart);
  if (!checksum::verify(checksum::xxh3_64, xxh3.data(), xxh3.size(),
                        buf.data() + kXxhOffset, kXxhSize)) {
    throw std::runtime_error("superblock XXH3-64 checksum verification failed");
  }

  auto const sha2 = buf.subspan(kShaStart);
  if (!checksum::verify(checksum::sha2_512_256, sha2.data(), sha2.size(),
                        buf.data() + kShaOffset, kShaSize)) {
    throw std::runtime_error(
        "superblock SHA2-512/256 checksum verification failed");
  }
}

void read_exactly(std::istream& input, std::byte* dest, std::size_t size,
                  char const* what) {
  input.read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(size));

  if (!input || input.gcount() != static_cast<std::streamsize>(size)) {
    throw std::runtime_error(std::string("failed to read ") + what);
  }
}

/// Read a superblock section from `input` and fully validate it. Used both
/// when initially reading the superblock and when re-checking the section
/// an update is about to overwrite. Returns both the parsed section and the
/// exact bytes it was parsed from.
parsed_superblock parse(std::istream& input) {
  parsed_superblock p;

  p.raw.resize(kHeaderSize);
  read_exactly(input, p.raw.data(), kHeaderSize, "superblock section header");

  std::memcpy(&p.section.hdr, p.raw.data(), kHeaderSize);

  // The length sizes the read below, i.e. it has to be trusted before the
  // checksums can confirm it, which is what `kMaxPayloadSize` guards.
  check_section_header(p.section.hdr);

  auto const payload_size = static_cast<std::size_t>(p.section.hdr.length);

  p.raw.resize(kHeaderSize + payload_size);
  read_exactly(input, p.raw.data() + kHeaderSize, payload_size, "superblock");

  verify_checksums(p.raw);

  std::memcpy(&p.section.sb, p.raw.data() + kHeaderSize, kKnownPayloadSize);

  check_superblock(p.section.sb);

  p.section.tail.assign(p.raw.begin() + kHeaderSize + kKnownPayloadSize,
                        p.raw.end());

  return p;
}

boost::uuids::uuid parse_uuid(std::string_view uuid) {
  if (uuid == superblock_editor::kUuidRandom) {
    return boost::uuids::random_generator()();
  }

  if (uuid == superblock_editor::kUuidNil) {
    return boost::uuids::nil_uuid();
  }

  boost::uuids::uuid parsed;

  try {
    parsed = boost::uuids::string_generator()(uuid.begin(), uuid.end());
  } catch (std::exception const&) {
    throw std::runtime_error("invalid filesystem UUID");
  }

  // An explicitly spelled-out nil UUID means the same as `kUuidNil`, and is
  // the one value allowed to deviate from the RFC 9562 layout.
  //
  // Only the variant is checked, not the version: older Boost releases
  // report `version_unknown` for versions 6 through 8, so checking the
  // version would reject perfectly valid UUIDs depending on which Boost
  // this was built against. The variant is what catches the mistake that
  // actually happens, namely a byte-swapped GUID.
  if (!parsed.is_nil() &&
      parsed.variant() != boost::uuids::uuid::variant_rfc_4122) {
    throw std::runtime_error(
        "filesystem UUID does not use the RFC 9562 variant");
  }

  return parsed;
}

} // namespace

class superblock_editor::impl {
 public:
  using digest_span = superblock_editor::digest_span;

  void read(std::istream& input);
  void update(std::iostream& io);

  std::streamoff image_offset() const {
    assert_loaded();
    return offset_;
  }

  std::size_t section_size() const {
    assert_loaded();
    return s_->size();
  }

  std::uint8_t major_version() const { return sb().major_version; }
  std::uint8_t minor_version() const { return sb().minor_version; }

  std::uint64_t fs_size_alignment() const {
    // Bounded by check_superblock(), so the shift is well-defined.
    return std::uint64_t{1} << sb().fs_size_align_log2;
  }

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
    // Null termination is guaranteed by check_superblock().
    return std::string_view{sb().fs_label.data()};
  }

  digest_algorithm digest_algo() const { return sb().digest_algo; }

  std::uint8_t digest_scheme_version() const {
    return sb().digest_scheme_version;
  }

  digest_span attr_digest() const { return digest_view(sb().attr_digest); }
  digest_span tree_digest() const { return digest_view(sb().tree_digest); }

  void init_fs_size(std::uint64_t fs_size) {
    auto& sblk = sb();

    if (sblk.fs_size != 0) {
      throw std::runtime_error("filesystem size has already been set");
    }

    // A size of zero is how "not set yet" is represented, and the size
    // must at least cover the superblock section itself.
    if (fs_size < s_->size()) {
      throw std::runtime_error("filesystem size is too small");
    }

    if (fs_size % fs_size_alignment() != 0) {
      throw std::runtime_error(
          "filesystem size is not a multiple of the alignment");
    }

    sblk.fs_size = fs_size;
    require_minor(kMinorFsSize);
  }

  void set_fs_uuid(std::string_view uuid) {
    // Parse before touching the superblock, so a rejected UUID leaves the
    // previous one intact.
    auto const parsed = parse_uuid(uuid);

    std::ranges::copy(parsed, sb().fs_uuid.begin());
    require_minor(kMinorFsUuid);
  }

  void set_fs_label(std::string_view label) {
    auto& raw = sb().fs_label;

    // Check everything before touching the superblock, so a rejected
    // label leaves the previous one intact.
    if (label.size() > superblock_editor::kMaxLabelLength) {
      throw std::runtime_error("filesystem label is too long");
    }

    if (label.find('\0') != std::string_view::npos) {
      throw std::runtime_error(
          "filesystem label must not contain null characters");
    }

    if (!utf8::is_valid(label.begin(), label.end())) {
      throw std::runtime_error("filesystem label is not valid UTF-8");
    }

    auto const end = std::ranges::copy(label, raw.begin()).out;
    std::fill(end, raw.end(), '\0');
    require_minor(kMinorFsLabel);
  }

  /// Algorithm, scheme and both digests are always written together: a tree
  /// digest computed under a different algorithm or scheme is meaningless,
  /// so an unspecified tree digest clears any existing one rather than
  /// leaving a stale value behind.
  void set_digests(digest_algorithm algo, std::uint8_t scheme_version,
                   digest_span attr, digest_span const* tree) {
    auto const size = get_digest_algorithm_size(algo);

    if (algo == digest_algorithm::UNINITIALIZED || size == 0) {
      throw std::runtime_error("invalid digest algorithm");
    }

    if (scheme_version == 0) {
      throw std::runtime_error("invalid digest scheme version");
    }

    check_digest(attr, size);

    if (tree) {
      check_digest(*tree, size);
    }

    auto& sblk = sb();

    sblk.digest_algo = algo;
    sblk.digest_scheme_version = scheme_version;
    store_digest(sblk.attr_digest, attr);
    store_digest(sblk.tree_digest, tree ? *tree : digest_span{});

    require_minor(kMinorDigests);
  }

  void set_tree_digest(digest_span tree) {
    auto& sblk = sb();
    auto const size = get_digest_algorithm_size(sblk.digest_algo);

    // Zero covers both "no digests at all" and "an algorithm we cannot
    // interpret"; in neither case do we know what a tree digest would mean.
    if (size == 0) {
      throw std::runtime_error(
          "cannot set a tree digest without an attribute digest");
    }

    check_digest(tree, size);

    store_digest(sblk.tree_digest, tree);
    require_minor(kMinorDigests);
  }

  void clear_digests() {
    auto& sblk = sb();
    sblk.digest_algo = digest_algorithm::UNINITIALIZED;
    sblk.digest_scheme_version = 0;
    sblk.attr_digest.fill(0);
    sblk.tree_digest.fill(0);
    require_minor(kMinorDigests);
  }

 private:
  digest_span digest_view(superblock_v1::digest_t const& digest) const {
    auto const size = get_digest_algorithm_size(sb().digest_algo);

    if (size == 0) {
      return {};
    }

    digest_span const view{digest.data(), size};

    return all_zero(view) ? digest_span{} : view;
  }

  static void check_digest(digest_span digest, std::size_t expected) {
    if (digest.size() != expected) {
      throw std::runtime_error("digest size does not match the algorithm");
    }

    // An all-zero digest is indistinguishable from an uninitialized one.
    if (all_zero(digest)) {
      throw std::runtime_error("refusing to store an all-zero digest");
    }
  }

  static void store_digest(superblock_v1::digest_t& dest, digest_span digest) {
    assert(digest.size() <= dest.size());
    dest.fill(0);
    std::ranges::copy(digest, dest.begin());
  }

  boost::uuids::uuid load_uuid() const {
    boost::uuids::uuid uuid;
    std::ranges::copy(sb().fs_uuid, uuid.begin());
    return uuid;
  }

  void require_minor(std::uint8_t version) {
    required_minor_ = std::max(required_minor_, version);
  }

  superblock_v1& sb() {
    assert_loaded();
    return s_->sb;
  }

  superblock_v1 const& sb() const {
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
  // ... and the exact bytes currently stored in the image.
  std::vector<std::byte> image_raw_;
  std::streamoff offset_{-1};
  std::uint8_t required_minor_{0};
};

void superblock_editor::impl::read(std::istream& input) {
  if (s_) {
    throw std::runtime_error("superblock has already been read");
  }

  auto const offset = input.tellg();

  // Only commit once everything has been validated. Otherwise a failed
  // read would leave unvalidated data in the editor, which a subsequent
  // update() could push into an image.
  auto p = parse(input);

  s_ = std::move(p.section);
  image_raw_ = std::move(p.raw);
  offset_ = offset < 0 ? -1 : static_cast<std::streamoff>(offset);
  required_minor_ = 0;
}

void superblock_editor::impl::update(std::iostream& io) {
  assert_loaded();

  if (offset_ < 0) {
    throw std::runtime_error("superblock was not read from a seekable stream");
  }

  // Build the new section first. Re-running the validation means a bug in
  // one of the setters cannot turn a valid image into an invalid one.
  auto s = *s_;

  s.sb.minor_version = std::max(s.sb.minor_version, required_minor_);

  check_section_header(s.hdr);
  check_superblock(s.sb);

  auto buf = s.serialize();

  // An update never changes the size of the section; anything else would
  // mean overwriting whatever follows the superblock.
  if (buf.size() != image_raw_.size()) {
    throw std::runtime_error("superblock size has changed unexpectedly");
  }

  update_checksums(buf);
  verify_checksums(buf);

  std::memcpy(&s.hdr, buf.data(), kHeaderSize);

  // Make sure the section we're about to overwrite is still a valid
  // superblock and still exactly the one we read. This catches both a
  // stream that doesn't refer to the image we read from and concurrent
  // modifications, either of which we'd otherwise silently destroy.
  io.seekg(offset_);

  if (!io) {
    throw std::runtime_error("failed to seek to superblock offset");
  }

  if (parse(io).raw != image_raw_) {
    throw std::runtime_error(
        "superblock in the image has changed since it was read");
  }

  io.seekp(offset_);

  if (!io) {
    throw std::runtime_error("failed to seek to superblock offset");
  }

  io.write(reinterpret_cast<char const*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
  io.flush();

  if (!io) {
    throw std::runtime_error("failed to write superblock");
  }

  if (auto const end = io.tellp();
      end < 0 || end - offset_ != static_cast<std::streamoff>(buf.size())) {
    throw std::runtime_error(
        "unexpected output position after writing superblock");
  }

  *s_ = std::move(s);
  image_raw_ = std::move(buf);
}

superblock_editor::superblock_editor()
    : impl_{std::make_unique<impl>()} {}

superblock_editor::~superblock_editor() = default;

superblock_editor::superblock_editor(superblock_editor&&) noexcept = default;
superblock_editor&
superblock_editor::operator=(superblock_editor&&) noexcept = default;

void superblock_editor::read(std::istream& input) { impl_->read(input); }

void superblock_editor::update(std::iostream& io) { impl_->update(io); }

std::streamoff superblock_editor::image_offset() const {
  return impl_->image_offset();
}

std::size_t superblock_editor::section_size() const {
  return impl_->section_size();
}

std::uint8_t superblock_editor::major_version() const {
  return impl_->major_version();
}

std::uint8_t superblock_editor::minor_version() const {
  return impl_->minor_version();
}

std::uint64_t superblock_editor::fs_size_alignment() const {
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

digest_algorithm superblock_editor::digest_algo() const {
  return impl_->digest_algo();
}

std::uint8_t superblock_editor::digest_scheme_version() const {
  return impl_->digest_scheme_version();
}

superblock_editor::digest_span superblock_editor::attr_digest() const {
  return impl_->attr_digest();
}

superblock_editor::digest_span superblock_editor::tree_digest() const {
  return impl_->tree_digest();
}

void superblock_editor::init_fs_size(std::uint64_t fs_size) {
  impl_->init_fs_size(fs_size);
}

void superblock_editor::set_fs_uuid(std::string_view uuid) {
  impl_->set_fs_uuid(uuid);
}

void superblock_editor::set_fs_label(std::string_view label) {
  impl_->set_fs_label(label);
}

void superblock_editor::set_digests(digest_algorithm algo,
                                    std::uint8_t scheme_version,
                                    digest_span attr_digest) {
  impl_->set_digests(algo, scheme_version, attr_digest, nullptr);
}

void superblock_editor::set_digests(digest_algorithm algo,
                                    std::uint8_t scheme_version,
                                    digest_span attr_digest,
                                    digest_span tree_digest) {
  impl_->set_digests(algo, scheme_version, attr_digest, &tree_digest);
}

void superblock_editor::set_tree_digest(digest_span tree_digest) {
  impl_->set_tree_digest(tree_digest);
}

void superblock_editor::clear_digests() { impl_->clear_digests(); }

} // namespace dwarfs
