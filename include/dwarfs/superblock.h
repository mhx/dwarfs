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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <dwarfs/boxed_endian.h>

namespace dwarfs {

constexpr uint8_t SUPERBLOCK_MAJOR_VERSION = 1;
constexpr uint8_t SUPERBLOCK_MINOR_VERSION = 1;

enum class digest_algorithm : uint8_t {
  UNINITIALIZED = 0,
  BLAKE3_256 = 1,
};

/**
 * File system superblock structure
 *
 * The superblock, if present, is always the first section in the file system.
 * It contains, among other things, the file system size, UUID, and label. The
 * superblock is always uncompressed and the data format is deliberately simple
 * to allow for easy parsing and validation.
 *
 * The superblock is versioned and has variable size. However, it is inherently
 * forwards-compatible: newer minor versions may add new fields, but older
 * versions will always be able to read the fields they know about. Older
 * versions must check fields like `digest_algo` and `digest_scheme_version`
 * to determine if they can read certain data in the superblock. The reserved
 * fields exist to potentially allow for adding information to the superblock
 * in-place without having to re-write the entire file system. A major version
 * change, though not expected, would indicate a breaking change in the
 * superblock format that older versions would be unable to read.
 *
 * Some fields in the superblock may be uninitialized. For example, the size
 * and digest fields can only be filled after the entire file system has been
 * written. If the file system is streamed to a pipe, there is no way for the
 * writer to initialize these fields. Also, the tree digest can be expensive
 * to compute, so it's an entirely optional feature. The UUID may also be nil
 * to support bit-identical file system image creation.
 *
 * Uninitialized fields are always fully zeroed. Except for the label and the
 * UUID, which can be changed at any time, only uninitialized fields may be
 * changed after the file system has been written. Once a field has been
 * initialized, it must no longer be changed. (It *could*, but tools generally
 * won't provide that functionality.) Note that updating any fields in the
 * superblock will require re-computing the checksums in the section header of
 * the superblock.
 *
 * The `fs_size` field, when initialized, represents the total size of the
 * file system image, excluding the header, but including all sections of
 * the file system. It must be a multiple of the alignment specified by
 * `fs_size_align_log2`.
 *
 * The attribute and tree digest fields, if initialized, allow for both
 * integrity checking as well as file system comparison. Both digests use the
 * same algorithm and scheme version, and share the same "spine" defined by
 * the scheme. The spine defines the order in which file system entries are
 * processed, as well as the core properties of each entry that are included
 * in the digest (typically, the type, name, and size of the entry).
 *
 * The tree digest includes a digest of the contents of each file system entry,
 * e.g. the data of regular files, the target of symlinks, and the recursive
 * digest of directories. Two file systems with the same tree digest are, with
 * overwhelming probability, identical in structure and content, provided both
 * use the same digest algorithm and scheme version. Also, the tree digest can
 * be used to verify the integrity of the file system contents.
 *
 * The attribute digest adds the attributes of the file system entries to the
 * spine. It does explicitly *not* include the content digests, which are
 * expensive to compute. However, it includes all attributes, e.g. permissions,
 * ownership, hardlink groups, timestamps, and extended attributes, except for
 * inode numbers and block counts. This makes it possible to compare file
 * systems created with options that affect inode numbers and block counts,
 * e.g. different inode ordering, or file systems built with `--hollow` or
 * `--no-sparse-files`.
 *
 * Two file system images with the same attribute digest *and* the same tree
 * digest are indistinguishable when mounted, except for the inode numbers and
 * block counts. Note that none of the digests cover the contents of the
 * superblock itself, or of sections / metadata that do not affect the mounted
 * file system. For example, the history, section index, compression type,
 * metadata packing, are explicitly *not* covered by these digests. This also
 * means that e.g. the file system label can be changed without affecting the
 * digests.
 */
struct superblock_v1 {
  static constexpr std::size_t kUuidSize = 16;
  static constexpr std::size_t kMaxLabelSize = 64;
  static constexpr std::size_t kMaxDigestSize = 32;

  using uuid_t = std::array<uint8_t, kUuidSize>;
  using label_t = std::array<char, kMaxLabelSize>;
  using digest_t = std::array<uint8_t, kMaxDigestSize>;

  // ---------------------------------------------------------------------------
  uint8_t major_version;        //   0  major version
  uint8_t minor_version;        //   1  minor version
  digest_algorithm digest_algo; //   2  digest algorithm
  uint8_t digest_scheme_version;//   3  digest canonicalization scheme version
  uint8_t fs_size_align_log2;   //   4  log2 of `fs_size` alignment in bytes
  uint8_t reserved1[3];         //   5  reserved for future use
  uint64le_t fs_size;           //   8  size in bytes, without header; must be
                                //      a multiple of `1 << fs_size_align_log2`
  uuid_t fs_uuid;               //  16  UUID of the file system in RFC 9562
                                //      network byte order (big-endian)
  label_t fs_label;             //  32  File system label, null-terminated and
                                //      null-padded UTF-8 string; max. 63 bytes
  digest_t attr_digest;         //  96  attribute digest using `digest_algo`,
                                //      see above for details
  digest_t tree_digest;         // 128  tree/content digest using `digest_algo`,
                                //      see above for details
  uint8_t reserved2[32];        // 160  reserved for future use
  // ---------------------------------------------------------------------------
};

bool is_known_digest_algorithm(digest_algorithm algo);
std::string get_digest_algorithm_name(digest_algorithm algo);

} // namespace dwarfs
