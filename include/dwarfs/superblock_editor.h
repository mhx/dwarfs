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

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <dwarfs/fstypes.h>
#include <dwarfs/superblock.h>

namespace dwarfs {

/**
 * In-place editor for the SUPERBLOCK section of a DwarFS image.
 *
 * Streams passed to this class *must* be opened in binary mode.
 *
 * Typical usage:
 *
 *     std::fstream f{path, std::ios::binary | std::ios::in | std::ios::out};
 *     f.seekg(image_offset);
 *
 *     superblock_editor ed;
 *     ed.read(f);
 *     ed.init_fs_size(total_section_size);
 *     ed.set_fs_uuid("random");
 *     ed.update(f);
 *
 * `read()` never modifies the image. `update()` is the only operation that
 * writes, and it writes exactly `section_size()` bytes at exactly the offset
 * the superblock was read from. Before doing so, it re-reads and re-validates
 * the section currently stored in the image and refuses to write unless it is
 * still byte-for-byte the section that was read, so a valid image can neither
 * be corrupted nor have concurrent modifications silently overwritten.
 *
 * Mutators are named for how often they may be called. An `init_` prefix means
 * the field can only be set once and a second attempt throws; everything else
 * may be changed freely, matching what the format permits.
 *
 * The payload size is taken from the section header rather than assumed, so a
 * superblock written by a future *minor* version - whether it added fields in
 * the reserved space or grew beyond `sizeof(superblock_v1)` - can still be
 * edited. Only fields known to this version of the code are interpreted;
 * everything else, including reserved fields and any trailing bytes, is
 * preserved verbatim. A superblock with an unknown *major* version is rejected
 * outright, since it may have relocated the fields this class writes.
 *
 * The minor version written back is the maximum of the minor version that was
 * read and the minor version required by the fields that were actually
 * modified, so editing an old superblock does not gratuitously bump it.
 */
class superblock_editor {
 public:
  static constexpr std::size_t kSectionHeaderSize = sizeof(section_header_v2);
  static constexpr std::size_t kMaxDigestSize = superblock_v1::kMaxDigestSize;

  /// Maximum length of a label in bytes, *excluding* the terminating null
  /// byte. The on-disk field is one byte larger.
  static constexpr std::size_t kMaxLabelLength =
      superblock_v1::kMaxLabelSize - 1;

  /// Accepted in place of a UUID by `set_fs_uuid()`.
  static constexpr std::string_view kUuidRandom{"random"};
  static constexpr std::string_view kUuidNil{"nil"};

  using digest_span = std::span<std::uint8_t const>;

  /// Smallest possible size of a superblock section, section header included.
  /// A superblock written by a future minor version may be larger, so this is
  /// a lower bound, not the size of any particular superblock.
  static constexpr std::size_t min_section_size() {
    return kSectionHeaderSize + sizeof(superblock_v1);
  }

  superblock_editor();
  ~superblock_editor();

  superblock_editor(superblock_editor&&) noexcept;
  superblock_editor& operator=(superblock_editor&&) noexcept;

  /// Read and validate a superblock from the current position of `input`.
  /// Throws if the data is not a valid superblock section, in which case
  /// the editor is left in its unread state.
  void read(std::istream& input);

  /// Overwrite the superblock in place, at the offset it was read from.
  /// `io` must refer to the same image `read()` was called on and must be
  /// open for both reading and writing.
  void update(std::iostream& io);

  /// Offset the superblock was read from, i.e. the image offset. Negative
  /// if it was read from a non-seekable stream, in which case `update()`
  /// cannot be used.
  std::streamoff image_offset() const;

  /// Size of the superblock section as found in the image, section header
  /// included. `update()` writes exactly this many bytes.
  std::size_t section_size() const;

  std::uint8_t major_version() const;
  std::uint8_t minor_version() const;

  std::uint64_t fs_size_alignment() const;
  std::optional<std::uint64_t> fs_size() const;

  /// The UUID in canonical form, or nullopt if the image carries none.
  std::optional<std::string> fs_uuid() const;

  /// The returned view is valid until the editor is modified or destroyed.
  std::string_view fs_label() const;

  /// The algorithm used for both digests, or `UNINITIALIZED` if the image
  /// carries none.
  digest_algorithm digest_algo() const;

  /// Version of the digest canonicalization scheme; zero if and only if no
  /// digests are present. Digests are only comparable between images that
  /// agree on both the algorithm and the scheme version.
  std::uint8_t digest_scheme_version() const;

  /// The digest, or an empty span if none is present. Note that the digests
  /// also read as empty when `digest_algo()` is an algorithm this version
  /// does not know, because how many of the stored bytes are significant
  /// follows from the algorithm; check `is_known_digest_algorithm()` to tell
  /// the two cases apart. The returned view is valid until the editor is
  /// modified or destroyed.
  digest_span attr_digest() const;
  /// As above; also empty for images without content digests, e.g. hollow
  /// images, even when `attr_digest()` is present.
  digest_span tree_digest() const;

  /// Can be called only if the size has not been set yet.
  void init_fs_size(std::uint64_t fs_size);

  /// Accepts a UUID in canonical form, `kUuidRandom` to generate a random
  /// (RFC 9562 version 4) UUID, or `kUuidNil` to remove the UUID. A UUID
  /// given explicitly must use the RFC 9562 variant, which also rejects a
  /// byte-swapped GUID.
  void set_fs_uuid(std::string_view uuid);

  /// The label must be valid UTF-8, must not contain null bytes, and must not
  /// exceed `kMaxLabelLength` bytes. An empty label removes it. On failure
  /// the previous label is left intact.
  void set_fs_label(std::string_view label);

  /// Store or replace the digests. Each digest must be exactly
  /// `get_digest_algorithm_size(algo)` bytes long. The replacement is atomic:
  /// the overload without a tree digest clears any existing one, because a
  /// tree digest computed under a different algorithm or scheme is
  /// meaningless.
  void set_digests(digest_algorithm algo, std::uint8_t scheme_version,
                   digest_span attr_digest);
  void set_digests(digest_algorithm algo, std::uint8_t scheme_version,
                   digest_span attr_digest, digest_span tree_digest);

  /// Store or replace the tree digest alone, keeping the algorithm and scheme
  /// already recorded. Throws unless the image already carries an attribute
  /// digest under a known algorithm.
  void set_tree_digest(digest_span tree_digest);

  void clear_digests();

 private:
  class impl;

  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
