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

namespace cpp dwarfs.thrift_lite.test

cpp_include "<unordered_map>"
cpp_include "<folly/container/sorted_vector_types.h>"

// ---- Typedefs / cpp.type mapping ----

typedef i16 UInt16 (cpp.type = "std::uint16_t")
typedef i32 UInt32 (cpp.type = "std::uint32_t")
typedef i64 UInt64 (cpp.type = "std::uint64_t")

typedef map<i32, string> IntToStringMap (cpp.template = "std::unordered_map")

typedef binary Blob

enum RecordKind {
  unknown = 0,
  file = 1,
  directory = 2,
  symlink = 3,
  device = 4,

  // leave a gap to make sure non-contiguous values are handled
  reserved_10 = 10,
}

struct SmallStrings {
  1: string name
  2: string comment
  3: optional string tag
  4: optional Blob payload
}

struct OnlyIntegral {
  1: i64 timestamp
  2: UInt16 flags
  3: UInt16 type
  4: UInt32 checksum
}

struct Header {
  1: i32 version
  2: optional i64 created_ts
  3: optional SmallStrings meta
  4: UInt16 flags
  5: optional RecordKind default_kind
}

struct BigRecord {
  1: i64 id
  2: RecordKind kind
  3: UInt32 checksum
  4: i64 offset
  5: i64 size
  6: bool deleted
  7: list<i64> extents
  8: list<UInt32> indices
}

struct Containers {
  // Regular containers
  1: list<i32> small_ints
  2: set<string> small_tags
  3: map<i32, string> id_to_name
  4: map<string, i64> name_to_value

  // Nested container
  5: list<list<i32>> nested_int_lists

  // Map with container values
  6: map<i32, list<i64>> id_to_extents

  // Optional containers
  7: optional list<i64> opt_list
  8: optional map<i32, string> opt_map (cpp.template = "folly::sorted_vector_map")
  9: optional set<i32> opt_set (cpp.template = "folly::sorted_vector_set")
}

// ---- Forward/backward compat exercises ----

struct CompatV1 {
  1: i32 a
  2: optional i32 b
  3: i64 c
  4: optional string d
  5: Containers containers

  // This one is meant to be removed in the "new" struct (V2)
  42: optional i32 removed_later
}

struct CompatV2 {
  1: i32 a
  2: optional i32 b
  3: i64 c
  4: optional string d
  5: Containers containers

  // Newly added fields
  6: optional i64 e
  7: optional SmallStrings f

  // Unknown/nested field to stress skip: older readers should skip it.
  50: optional map<i32, list<SmallStrings>> future_field
}

struct TestMessage {
  1: Header header

  2: optional SmallStrings labels

  3: list<BigRecord> records

  4: Containers containers

  5: CompatV1 v1
  6: CompatV2 v2

  100: optional i32 far_optional
  300: i32 far_regular
}
