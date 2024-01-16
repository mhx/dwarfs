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
 */

#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <folly/json.h>

#include "dwarfs/compression_metadata_requirements.h"
#include "dwarfs/map_util.h"

using namespace dwarfs;

TEST(metadata_requirements, dynamic_test) {
  std::string requirements = R"({
    "compression": ["set", ["lz4", "zstd"]],
    "block_size": ["range", 16, 1024],
    "channels": ["set", [1, 2, 4]]
  })";

  std::unique_ptr<compression_metadata_requirements<folly::dynamic>> req;

  ASSERT_NO_THROW(
      req = std::make_unique<compression_metadata_requirements<folly::dynamic>>(
          requirements));
  {
    std::string metadata = R"({
      "compression": "lz4",
      "block_size": 256,
      "channels": 2
    })";

    EXPECT_NO_THROW(req->check(metadata));
  }

  {
    std::string metadata = R"({
      "compression": "lz4",
      "foo": "bar",
      "block_size": 256,
      "channels": 2
    })";

    EXPECT_NO_THROW(req->check(metadata));
  }

  {
    std::string metadata = R"({
      "compression": "lzma",
      "block_size": 256,
      "channels": 2
    })";

    EXPECT_THAT(
        [&] { req->check(metadata); },
        ThrowsMessage<std::runtime_error>(testing::HasSubstr(
            "compression 'lzma' does not meet requirements [lz4, zstd]")));
  }

  {
    std::string metadata = R"({
      "block_size": 256,
      "channels": 2
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(
                    testing::HasSubstr("missing requirement 'compression'")));
  }

  {
    std::string metadata = R"({
      "compression": "zstd",
      "block_size": 8,
      "channels": 2
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "block_size '8' does not meet requirements [16, 1024]")));
  }

  {
    std::string metadata = R"({
      "compression": "zstd",
      "block_size": "foo",
      "channels": 2
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "non-integral type for requirement 'block_size', "
                    "got type 'string'")));
  }

  {
    std::string metadata = R"({
      "compression": 13,
      "block_size": 256,
      "channels": 2
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "non-string type for requirement 'compression', "
                    "got type 'int64'")));
  }

  {
    std::string metadata = R"({
      "compression": 13,
      "block_size": 256,
      "channels": "foo"
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "non-integral type for requirement 'channels', "
                    "got type 'string'")));
  }

  {
    std::string metadata = R"({
      "compression": 13,
      "block_size": 256,
      "channels": 3
    })";

    EXPECT_THAT([&] { req->check(metadata); },
                ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                    "channels '3' does not meet requirements [1, 2, 4]")));
  }
}

TEST(metadata_requirements, dynamic_test_error) {
  using namespace std::literals::string_literals;
  using req_type = compression_metadata_requirements<folly::dynamic>;

  EXPECT_THAT(
      [&] { req_type tmp(R"([])"s); },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "metadata requirements must be an object, got type 'array'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": 42
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "requirement 'compression' must be an array, got type 'int64'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": [1]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "requirement 'compression' must be an array of at least 2 elements, "
          "got only 1")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": [1, 2]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "type for requirement 'compression' must be a string, got type "
          "'int64'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["foo", 2]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(
          testing::HasSubstr("unsupported requirement type 'foo'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["range", 2]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(
          testing::HasSubstr("unexpected array size 2 for requirement "
                             "'compression', expected 3")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["range", "foo", 42]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "could not parse minimum value 'foo' for requirement 'compression': "
          "Invalid leading character: \"foo\"")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["range", 43, 42]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "expected minimum '43' to be less than or equal to maximum '42' for "
          "requirement 'compression'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["set", 2]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(
          testing::HasSubstr("set for requirement 'compression' must be an "
                             "array, got type 'int64'")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["set", []]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "set for requirement 'compression' must not be empty")));

  EXPECT_THAT(
      [&] {
        req_type tmp(R"({
          "compression": ["set", ["foo", "bar", "foo"]]
        })"s);
      },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "duplicate value 'foo' for requirement 'compression'")));
}

namespace {

enum class test_enum { foo, bar, baz };

struct test_metadata {
  test_enum enum_value;
  std::string string_value;
  int16_t int16_value;
  uint32_t uint32_value;
};

std::optional<test_enum> parse_enum(folly::dynamic const& value) {
  static std::unordered_map<std::string_view, test_enum> const enum_map{
      {"foo", test_enum::foo},
      {"bar", test_enum::bar},
      {"baz", test_enum::baz}};
  return get_optional(enum_map, value.asString());
}

std::ostream& operator<<(std::ostream& os, test_enum e) {
  switch (e) {
  case test_enum::foo:
    return os << "foo";
  case test_enum::bar:
    return os << "bar";
  case test_enum::baz:
    return os << "baz";
  }
  throw std::runtime_error("invalid enum value");
}

} // namespace

template <>
struct fmt::formatter<test_enum> : ostream_formatter {};

class metadata_requirements_test : public ::testing::Test {
 public:
  void SetUp() override {
    req = std::make_unique<compression_metadata_requirements<test_metadata>>();
    req->add_set("enum", &test_metadata::enum_value, parse_enum);
    req->add_set<std::string>("string", &test_metadata::string_value);
    req->add_range<int>("int16", &test_metadata::int16_value);
    req->add_set<int64_t>("uint32", &test_metadata::uint32_value);
  }

  void TearDown() override { req.reset(); }

  std::unique_ptr<compression_metadata_requirements<test_metadata>> req;
};

TEST_F(metadata_requirements_test, static_test) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["foo"]],
    "string": ["set", ["cat", "dog"]],
    "int16": ["range", -1024, 1024],
    "uint32": ["set", [1, 2, 3, 5]]
  })");

  ASSERT_NO_THROW(req->parse(dyn));

  test_metadata metadata{
      .enum_value = test_enum::foo,
      .string_value = "cat",
      .int16_value = 256,
      .uint32_value = 5,
  };

  EXPECT_NO_THROW(req->check(metadata));

  metadata.enum_value = test_enum::bar;

  EXPECT_THAT([&]() { req->check(metadata); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "enum 'bar' does not meet requirements [foo]")));

  metadata.enum_value = test_enum::foo;
  metadata.string_value = "dog";

  EXPECT_NO_THROW(req->check(metadata));

  metadata.string_value = "mouse";

  EXPECT_THAT([&]() { req->check(metadata); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "string 'mouse' does not meet requirements [cat, dog]")));

  metadata.string_value = "cat";
  metadata.int16_value = -1024;

  EXPECT_NO_THROW(req->check(metadata));

  metadata.int16_value = 1024;

  EXPECT_NO_THROW(req->check(metadata));

  metadata.int16_value = -1025;

  EXPECT_THAT([&]() { req->check(metadata); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "int16 '-1025' does not meet requirements [-1024..1024]")));

  metadata.int16_value = 1025;

  EXPECT_THAT([&]() { req->check(metadata); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "int16 '1025' does not meet requirements [-1024..1024]")));

  metadata.int16_value = 0;
  metadata.uint32_value = 1;

  EXPECT_NO_THROW(req->check(metadata));

  metadata.uint32_value = 5;

  EXPECT_NO_THROW(req->check(metadata));

  metadata.uint32_value = 4;

  EXPECT_THAT([&]() { req->check(metadata); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "uint32 '4' does not meet requirements [1, 2, 3, 5]")));
}

TEST_F(metadata_requirements_test, static_test_unsupported) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["foo"]],
    "string": ["set", ["cat", "dog"]],
    "int16": ["range", -1024, 1024],
    "uint32": ["set", [1, 2, 3, 5]],
    "strange": ["set", ["foo", "bar"]]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "unsupported metadata requirements: strange")));
}

TEST_F(metadata_requirements_test, static_test_less_strict) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["foo"]],
    "int16": ["range", -1024, 1024]
  })");

  ASSERT_NO_THROW(req->parse(dyn));

  test_metadata metadata{
      .enum_value = test_enum::foo,
      .string_value = "cat",
      .int16_value = 256,
      .uint32_value = 5,
  };

  EXPECT_NO_THROW(req->check(metadata));
}

TEST_F(metadata_requirements_test, static_test_req_error_non_object) {
  auto dyn = folly::parseJson(R"([])");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "TypeError: expected dynamic type `object', "
                  "but had type `array'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_non_array) {
  auto dyn = folly::parseJson(R"({
    "enum": 42
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "found non-array type for requirement 'enum', "
                  "got type 'int64'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_empty_array) {
  auto dyn = folly::parseJson(R"({
    "enum": []
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "unexpected empty value for requirement 'enum'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_wrong_type) {
  auto dyn = folly::parseJson(R"({
    "enum": [17]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "invalid type '17' for requirement 'enum', "
                  "expected 'set'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_unexpected_type) {
  auto dyn = folly::parseJson(R"({
    "enum": ["range"]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "invalid type 'range' for requirement 'enum', "
                  "expected 'set'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_invalid_set1) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set"]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "unexpected array size 1 for requirement 'enum', "
                  "expected 2")));
}

TEST_F(metadata_requirements_test, static_test_req_error_invalid_set2) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", 42]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "non-array type argument for requirement 'enum', "
                  "got 'int64'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_empty_set) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", []]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "unexpected empty set for requirement 'enum'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_invalid_set3) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["grmpf"]]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "no supported values for requirement 'enum'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_invalid_set4) {
  auto dyn = folly::parseJson(R"({
    "uint32": ["set", ["grmpf"]]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "could not parse set value 'grmpf' for requirement 'uint32': "
                  "Invalid leading character: \"grmpf\"")));
}

TEST_F(metadata_requirements_test, static_test_req_set_with_invalid_value) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["grmpf", "foo"]]
  })");

  EXPECT_NO_THROW(req->parse(dyn));
}

TEST_F(metadata_requirements_test, static_test_req_error_invalid_set5) {
  auto dyn = folly::parseJson(R"({
    "enum": ["set", ["grmpf", "foo", "foo"]]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "duplicate value 'foo' for requirement 'enum'")));
}

TEST_F(metadata_requirements_test, static_test_req_error_range_invalid1) {
  auto dyn = folly::parseJson(R"({
    "int16": ["range"]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "unexpected array size 1 for requirement 'int16', "
                  "expected 3")));
}

TEST_F(metadata_requirements_test, static_test_req_error_range_invalid2) {
  auto dyn = folly::parseJson(R"({
    "int16": ["range", "bla", 17]
  })");

  EXPECT_THAT(
      [&]() { req->parse(dyn); },
      ThrowsMessage<std::runtime_error>(testing::HasSubstr(
          "could not parse minimum value 'bla' for requirement 'int16': "
          "Invalid leading character: \"bla\"")));
}

TEST_F(metadata_requirements_test, static_test_req_error_range_invalid3) {
  auto dyn = folly::parseJson(R"({
    "int16": ["range", 18, 17]
  })");

  EXPECT_THAT([&]() { req->parse(dyn); },
              ThrowsMessage<std::runtime_error>(testing::HasSubstr(
                  "expected minimum '18' to be less than or equal to "
                  "maximum '17' for requirement 'int16'")));
}
