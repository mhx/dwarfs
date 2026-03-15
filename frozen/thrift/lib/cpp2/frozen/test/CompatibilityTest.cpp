/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <array>
#include <filesystem>

#include <gtest/gtest.h>

#include <dwarfs/file_util.h>
#include <dwarfs/util.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Compatibility_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Compatibility_types.h>

using namespace apache::thrift::frozen;
using namespace apache::thrift::test;

namespace {

auto const kCompatDataDir =
    std::filesystem::path(FROZEN_COMPAT_DATA_DIR).make_preferred();

auto make_person(std::string name, std::optional<double> dob = std::nullopt)
    -> Person {
  Person person;
  person.name() = std::move(name);
  if (dob) {
    person.dob() = *dob;
  }
  return person;
}

auto make_root(std::string title, std::unordered_map<int64_t, Person> people)
    -> Root {
  Root root;
  root.title() = std::move(title);
  root.people() = std::move(people);
  return root;
}

struct Case {
  std::string name;
  std::optional<Root> root;
  bool fails{false};

  friend std::ostream& operator<<(std::ostream& os, const Case& c) {
    return os << c.name;
  }
};

const std::array kTestCases{
    Case{
        "beforeUnique",
        make_root(
            "version 0",
            {
                {101, make_person("alice", 1.23e9)},
                {102, make_person("bob", 1.21e9)},
            }),
    },
    Case{
        "afterUnique",
        make_root(
            "version 0",
            {
                {101, make_person("alice", 1.23e9)},
                {102, make_person("bob", 1.21e9)},
            }),
    },
    Case{
        "withFileVersion",
        make_root(
            "version 0",
            {
                {101, make_person("alice", 1.23e9)},
                {102, make_person("bob", 1.21e9)},
            }),
    },
    Case{"futureVersion", std::nullopt, true},
    Case{"missing", std::nullopt, true},
};

} // namespace

class CompatibilityTest : public ::testing::TestWithParam<Case> {
 public:
  static std::filesystem::path filePath(std::string_view name) {
    return kCompatDataDir / name;
  }
};

TEST_P(CompatibilityTest, Write) {
  if (!dwarfs::getenv_is_enabled("FROZEN_COMPAT_WRITE_FILES")) {
    return;
  }
  auto test = GetParam();
  if (test.root) {
    dwarfs::write_file(filePath(test.name), freezeToString(*test.root));
  }
}

TEST_P(CompatibilityTest, Read) {
  auto test = GetParam();
  auto path = filePath(test.name);

  try {
    auto root = mapFrozen<Root>(dwarfs::read_file(path));
    EXPECT_FALSE(test.fails);
    EXPECT_EQ(test.root.value(), root.thaw());
  } catch (const std::exception&) {
    EXPECT_TRUE(test.fails);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllCases, CompatibilityTest, ::testing::ValuesIn(kTestCases));
