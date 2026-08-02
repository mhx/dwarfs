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
 *     ed.init_fs_uuid();
 *     ed.update(f);
 *
 * `read()` never modifies the image. `update()` is the only operation that
 * writes, and it writes exactly `superblock_size()` bytes at exactly the
 * offset the superblock was read from. Before doing so, it re-reads and
 * re-validates the section currently stored in the image and refuses to
 * write unless it is still byte-for-byte the section that was read, so a
 * valid image can neither be corrupted nor have concurrent modifications
 * silently overwritten.
 *
 * Only fields known to this version of the code are interpreted; anything
 * else, including reserved fields and fields added by a future superblock
 * version, is preserved verbatim.
 */
class superblock_editor {
 public:
  static constexpr std::size_t kUuidSize = superblock_v0::kUuidSize;
  static constexpr std::size_t kSuperblockSize = superblock_v0::kSuperblockSize;

  superblock_editor();
  ~superblock_editor();

  superblock_editor(superblock_editor&&) noexcept;
  superblock_editor& operator=(superblock_editor&&) noexcept;

  /// Size of the superblock section, section header included.
  static constexpr std::size_t superblock_size() { return kSuperblockSize; }

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

  std::uint32_t fs_size_alignment() const;
  std::optional<std::uint64_t> fs_size() const;

  std::optional<std::string> fs_uuid() const;
  /// The returned view is valid until the editor is modified or destroyed.
  std::string_view fs_label() const;

  // Can be called only if the size has not been set yet.
  void init_fs_size(std::uint64_t fs_size);
  // Can be called only if the UUID has not been set yet.
  void init_fs_uuid();
  // Same, but with a caller-provided UUID (must not be nil).
  void init_fs_uuid(std::span<std::uint8_t const, kUuidSize> uuid);

  void set_fs_label(std::string_view label);

 private:
  class impl;

  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
