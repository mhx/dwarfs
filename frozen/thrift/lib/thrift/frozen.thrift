/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

cpp_include "<parallel_hashmap/btree.h>"

namespace cpp apache.thrift.frozen.schema

struct Field {
  // layout id, indexes into layouts
  1: i16 layoutId;
  // field offset:
  //  < 0: -(bit offset)
  //  >= 0: byte offset
  2: i16 offset;
}

struct Layout {
  1: i32 size;
  2: i16 bits;
  3: map<i16, Field> fields (cpp.template = "phmap::btree_map");
  4: string typeName;
}

// const i32 kCurrentFrozenFileVersion = 1;

struct Schema {
  // File format version, incremented on breaking changes to Frozen2
  // implementation.  Only backwards-compatibility is guaranteed.
  4: i32 fileVersion;
  // Field type names may not change unless relaxTypeChecks is set.
  1: bool relaxTypeChecks;
  2: map<i16, Layout> layouts (cpp.template = "phmap::btree_map");
  3: i16 rootLayout;
}
