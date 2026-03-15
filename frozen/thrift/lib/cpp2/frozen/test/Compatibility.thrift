/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

namespace cpp apache.thrift.test

cpp_include "<unordered_map>"

struct Person {
  3: optional double dob;
  5: string name;
}

struct Root {
  1: string title;
  2: map<i64, Person> people (cpp.template = "std::unordered_map");
}
