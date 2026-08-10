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
 * Versioning
 *
 * The struct name tracks the *major* version only; a minor version bump does
 * not introduce a new struct. A major version bump indicates a breaking change
 * and readers must reject any major version they don't know. Major version 0
 * is permanently reserved: it was used by an early draft of this structure and
 * is not supported.
 *
 * Minor versions are strictly additive, and readers must accept minor versions
 * they don't know. A new field is added either within the reserved space, in
 * which case the section length does not change, or by growing the structure,
 * in which case it does. Readers must therefore take the payload size from the
 * section header rather than assuming `sizeof(superblock_v1)`, must accept a
 * larger payload, and must preserve any bytes they don't understand. Writers
 * must never lower the minor version of an existing superblock, and must raise
 * it when writing a field introduced by a later minor version.
 *
 * Initialization
 *
 * Some fields in the superblock may be uninitialized. For example, the size
 * and digest fields can only be filled after the entire file system has been
 * written. If the file system is streamed to a pipe, there is no way for the
 * writer to initialize these fields. Also, the digests can be expensive to
 * compute, so they are an entirely optional feature. The UUID may also be nil
 * to support bit-identical file system image creation.
 *
 * Uninitialized fields are always fully zeroed. Except for the label, the UUID
 * and the digests, only uninitialized fields may be changed after the file
 * system has been written. Note that updating any field in the superblock
 * requires re-computing the checksums in the section header of the superblock.
 *
 * Digests
 *
 * The attribute and tree digest fields, if initialized, allow for both
 * integrity checking as well as file system comparison. Both digests use the
 * same algorithm and scheme version, and share the same "spine" defined by
 * the scheme. The spine defines the order in which file system entries are
 * processed, as well as the core properties of each entry that are included
 * in the digest (typically, the type, name, and size of the entry). Digests
 * are only comparable between images that agree on both `digest_algo` and
 * `digest_scheme_version`.
 *
 * The tree digest includes a digest of the contents of each file system entry,
 * e.g. the data of regular files, the target of symlinks, and the recursive
 * digest of directories. Two file systems with the same tree digest are, with
 * overwhelming probability, identical in structure and content. Also, the tree
 * digest can be used to verify the integrity of the file system contents.
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
 * file system. For example, the history, section index, compression type and
 * metadata packing are explicitly *not* covered by these digests. This also
 * means that e.g. the file system label can be changed without affecting the
 * digests.
 *
 * A tree digest must not be stored without an attribute digest; the reverse is
 * allowed and is how e.g. hollow images can avoid computing a meaningless tree
 * digest. Whenever `digest_algo` or `digest_scheme_version` changes, both
 * digests must be replaced together: a digest computed under a different
 * scheme is meaningless.
 *
 * The digest fields are sized for the longest supported digest. An algorithm
 * with a shorter digest stores it left-aligned, and the remaining bytes must
 * be zero.
 *
 * Size
 *
 * The `fs_size` field, when initialized, is the total size of the file system
 * image, measured from the start of the first section. It excludes any header
 * prepended to the image, so that adding or removing a header does not
 * invalidate it, and it includes every section of the file system, including
 * any trailing padding. It must be a multiple of the alignment specified by
 * `fs_size_align_log2`.
 */
struct superblock_v1 {
  static constexpr std::size_t kUuidSize = 16;
  static constexpr std::size_t kMaxLabelSize = 64;
  static constexpr std::size_t kMaxDigestSize = 32;

  using uuid_t = std::array<uint8_t, kUuidSize>;
  using label_t = std::array<char, kMaxLabelSize>;
  using digest_t = std::array<uint8_t, kMaxDigestSize>;

  // ---------------------------------------------------------------------------
  uint8_t major_version;         //   0 major version; never zero
  uint8_t minor_version;         //   1 minor version; never zero
  digest_algorithm digest_algo;  //   2 digest algorithm; UNINITIALIZED means
                                 //     no digests are present
  uint8_t digest_scheme_version; //   3 digest canonicalization scheme version;
                                 //     zero if and only if `digest_algo` is
                                 //     UNINITIALIZED
  uint8_t fs_size_align_log2;    //   4 log2 of `fs_size` alignment in bytes;
                                 //     zero means no alignment
  uint8_t reserved1[3];          //   5 reserved for future use
  uint64le_t fs_size;            //   8 size in bytes, without header; must be
                                 //     a multiple of `1 << fs_size_align_log2`
  uuid_t fs_uuid;                //  16 UUID of the file system in RFC 9562
                                 //     byte order (big-endian fields)
  label_t fs_label;              //  32 file system label, null-terminated and
                                 //     null-padded UTF-8 string; at most 63
                                 //     bytes, so the last byte is always zero
  digest_t attr_digest;          //  96 attribute digest using `digest_algo`,
                                 //     see above for details
  digest_t tree_digest;          // 128 tree/content digest using `digest_algo`,
                                 //     see above for details; must be zero if
                                 //     `attr_digest` is zero
  uint8_t reserved2[32];         // 160 reserved for future use
  // ---------------------------------------------------------------------------
};

// The superblock is (de)serialized as raw bytes, and both `fs_uuid` and
// `fs_label` must stay at a fixed offset across all minor versions so that
// in-place editors never need to care about the minor version.
static_assert(sizeof(superblock_v1) == 192);
static_assert(offsetof(superblock_v1, major_version) == 0);
static_assert(offsetof(superblock_v1, minor_version) == 1);
static_assert(offsetof(superblock_v1, digest_algo) == 2);
static_assert(offsetof(superblock_v1, digest_scheme_version) == 3);
static_assert(offsetof(superblock_v1, fs_size_align_log2) == 4);
static_assert(offsetof(superblock_v1, reserved1) == 5);
static_assert(offsetof(superblock_v1, fs_size) == 8);
static_assert(offsetof(superblock_v1, fs_uuid) == 16);
static_assert(offsetof(superblock_v1, fs_label) == 32);
static_assert(offsetof(superblock_v1, attr_digest) == 96);
static_assert(offsetof(superblock_v1, tree_digest) == 128);
static_assert(offsetof(superblock_v1, reserved2) == 160);

bool is_known_digest_algorithm(digest_algorithm algo);
std::string get_digest_algorithm_name(digest_algorithm algo);

// Number of significant digest bytes for `algo`, at most
// `superblock_v1::kMaxDigestSize`. Zero for `UNINITIALIZED`
// and for any algorithm this version does not know.
std::size_t get_digest_algorithm_size(digest_algorithm algo);

} // namespace dwarfs
