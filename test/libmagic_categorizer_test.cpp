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

#include <atomic>
#include <exception>
#include <filesystem>
#include <latch>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/writer/categorizer.h>

#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_logger.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using dwarfs::test::loremipsum;
using dwarfs::test::make_mock_file_view;
using namespace std::string_view_literals;

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

std::string png_data(size_t payload = 64) {
  std::string data{"\x89PNG\x0d\x0a\x1a\x0a\x00\x00\x00\x0dIHDR", 16};
  data.resize(16 + payload, '\0');
  return data;
}

std::string gzip_data(size_t payload = 64) {
  std::string data{"\x1f\x8b\x08\x00", 4};
  data.resize(4 + payload, '\0');
  return data;
}

std::string text_then_binary(size_t text_prefix, size_t total) {
  auto data = loremipsum(text_prefix);
  data.resize(total, '\x01');
  return data;
}

} // namespace

template <typename Base>
class libmagic_categorizer_fixture : public Base {
 protected:
  void SetUp() override { lgr.clear(); }

  void create_catmgr() { create_catmgr({}); }

  void create_catmgr(std::vector<char const*> args) {
    writer::categorizer_registry catreg;

    po::options_description opts;
    catreg.add_options(opts);

    args.insert(args.begin(), "program");

    po::variables_map vm;
    auto parsed = po::parse_command_line(args.size(), args.data(), opts);

    po::store(parsed, vm);
    po::notify(vm);

    catmgr = std::make_shared<writer::categorizer_manager>(lgr, "/");

    catmgr->add(catreg.create(lgr, "libmagic", vm, nullptr));
  }

 public:
  auto categorize(fs::path const& path, file_view const& mm) {
    auto job = catmgr->job(path);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    return job.result();
  }

  std::string category_of(fs::path const& path, file_view const& mm) {
    auto frag = categorize(path, mm);
    if (frag.size() != 1) {
      return "<none>";
    }
    return std::string(catmgr->category_name(frag[0].category().value()));
  }

  std::shared_ptr<writer::categorizer_manager> catmgr;
  test::test_logger lgr{logger::INFO};
};

using libmagic_categorizer = libmagic_categorizer_fixture<::testing::Test>;

TEST_F(libmagic_categorizer, no_categories_defined) {
  create_catmgr();

  EXPECT_FALSE(catmgr->category_value("text").has_value());

  auto mm = make_mock_file_view(png_data());
  EXPECT_TRUE(categorize("test.png", mm).empty());
}

TEST_F(libmagic_categorizer, category_is_registered) {
  create_catmgr({"--libmagic-category=text=text/*"});

  EXPECT_TRUE(catmgr->category_value("text").has_value());
}

TEST_F(libmagic_categorizer, category_matching_nothing_is_still_registered) {
  create_catmgr({"--libmagic-category=video=video/*"});

  EXPECT_TRUE(catmgr->category_value("video").has_value());

  auto mm = make_mock_file_view(loremipsum(4096));
  EXPECT_TRUE(categorize("ipsum.txt", mm).empty());
}

TEST_F(libmagic_categorizer, simple_glob_match) {
  create_catmgr({"--libmagic-category=text=text/*"});

  auto mm = make_mock_file_view(loremipsum(4096));
  auto frag = categorize("ipsum.txt", mm);

  ASSERT_EQ(1, frag.size());
  EXPECT_EQ("text", catmgr->category_name(frag[0].category().value()));
}

TEST_F(libmagic_categorizer, multiple_patterns_per_category) {
  create_catmgr({"--libmagic-category=binary=image/png:application/gzip"});

  {
    auto mm = make_mock_file_view(png_data());
    EXPECT_EQ("binary", category_of("test.png", mm));
  }

  {
    auto mm = make_mock_file_view(gzip_data());
    EXPECT_EQ("binary", category_of("test.gz", mm));
  }
}

TEST_F(libmagic_categorizer, repeated_option_merges_into_one_category) {
  create_catmgr({"--libmagic-category=binary=image/png",
                 "--libmagic-category=binary=application/gzip"});

  auto const cat = catmgr->category_value("binary");
  ASSERT_TRUE(cat.has_value());

  {
    auto mm = make_mock_file_view(png_data());
    auto frag = categorize("test.png", mm);
    ASSERT_EQ(1, frag.size());
    EXPECT_EQ(cat.value(), frag[0].category().value());
  }

  {
    auto mm = make_mock_file_view(gzip_data());
    auto frag = categorize("test.gz", mm);
    ASSERT_EQ(1, frag.size());
    EXPECT_EQ(cat.value(), frag[0].category().value());
  }
}

TEST_F(libmagic_categorizer, first_matching_category_wins) {
  create_catmgr({"--libmagic-category=broad=image/*",
                 "--libmagic-category=narrow=image/png"});

  auto mm = make_mock_file_view(png_data());
  EXPECT_EQ("broad", category_of("test.png", mm));
}

TEST_F(libmagic_categorizer, first_matching_category_wins_reversed) {
  create_catmgr({"--libmagic-category=narrow=image/png",
                 "--libmagic-category=broad=image/*"});

  auto mm = make_mock_file_view(png_data());
  EXPECT_EQ("narrow", category_of("test.png", mm));
}

TEST_F(libmagic_categorizer, fragment_covers_whole_file) {
  create_catmgr({"--libmagic-category=text=text/*", "--libmagic-max-bytes=1K"});

  auto const size = 256_KiB;
  auto mm = make_mock_file_view(loremipsum(size));
  auto frag = categorize("big.txt", mm);

  ASSERT_EQ(1, frag.size());
  EXPECT_EQ(size, frag[0].size());
  EXPECT_EQ(size, frag.total_size());
}

TEST_F(libmagic_categorizer, max_bytes_bounds_inspection) {
  auto const data = text_then_binary(4_KiB, 32_KiB);

  {
    create_catmgr({"--libmagic-category=text=text/*",
                   "--libmagic-category=binary=application/octet-stream",
                   "--libmagic-max-bytes=2K"});
    auto mm = make_mock_file_view(data);
    EXPECT_EQ("text", category_of("mixed.bin", mm));
  }

  {
    create_catmgr({"--libmagic-category=text=text/*",
                   "--libmagic-category=binary=application/octet-stream",
                   "--libmagic-max-bytes=32K"});
    auto mm = make_mock_file_view(data);
    EXPECT_EQ("binary", category_of("mixed.bin", mm));
  }
}

TEST_F(libmagic_categorizer, file_smaller_than_max_bytes) {
  create_catmgr(
      {"--libmagic-category=image=image/*", "--libmagic-max-bytes=64K"});

  auto mm = make_mock_file_view(png_data(16));
  EXPECT_EQ("image", category_of("tiny.png", mm));
}

TEST_F(libmagic_categorizer, empty_file_does_not_throw) {
  create_catmgr({"--libmagic-category=text=text/*"});

  auto mm = make_mock_file_view(std::string{});
  EXPECT_NO_THROW(categorize("empty.txt", mm));
}

TEST_F(libmagic_categorizer, invalid_database_path) {
  create_catmgr({"--libmagic-category=text=text/*",
                 "--libmagic-database=/nonexistent/magic.mgc"});

  auto mm = make_mock_file_view(loremipsum(4096));

  EXPECT_THAT([&] { categorize("ipsum.txt", mm); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  ::testing::HasSubstr("magic_load")));
}

TEST_F(libmagic_categorizer, repeated_categorization_is_stable) {
  create_catmgr({"--libmagic-category=image=image/png",
                 "--libmagic-category=text=text/*"});

  auto const png = png_data();
  auto const txt = loremipsum(4096);

  for (int i = 0; i < 100; ++i) {
    auto mm_png = make_mock_file_view(png);
    auto mm_txt = make_mock_file_view(txt);
    EXPECT_EQ("image", category_of("test.png", mm_png)) << "iteration " << i;
    EXPECT_EQ("text", category_of("ipsum.txt", mm_txt)) << "iteration " << i;
  }
}

TEST_F(libmagic_categorizer, concurrent_categorization) {
  create_catmgr({"--libmagic-category=image=image/png",
                 "--libmagic-category=text=text/*"});

  auto const png = png_data();
  auto const txt = loremipsum(4096);

  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  static constexpr int kNumThreads = 8;

  std::latch start_latch(kNumThreads);

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      start_latch.arrive_and_wait();
      try {
        for (int i = 0; i < 50; ++i) {
          auto const& data = (t + i) % 2 ? png : txt;
          auto const expected = (t + i) % 2 ? "image" : "text";
          auto mm = make_mock_file_view(data);
          if (category_of("test", mm) != expected) {
            ++failures;
          }
        }
      } catch (...) {
        ++failures;
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  EXPECT_EQ(0, failures.load());
}

TEST_F(libmagic_categorizer, subcategory_less_is_false) {
  create_catmgr({"--libmagic-category=text=text/*"});

  auto const cat = catmgr->category_value("text").value();
  writer::fragment_category const a{cat, 0};
  writer::fragment_category const b{cat, 1};

  EXPECT_FALSE(catmgr->deterministic_less(a, b));
  EXPECT_FALSE(catmgr->deterministic_less(b, a));
}

using invalid_definition_test = libmagic_categorizer_fixture<
    ::testing::TestWithParam<std::pair<std::string_view, std::string_view>>>;

TEST_P(invalid_definition_test, rejected) {
  auto const [def, expected] = GetParam();
  auto const arg = fmt::format("--libmagic-category={}", def);

  EXPECT_THAT([&] { create_catmgr({arg.c_str()}); },
              ::testing::ThrowsMessage<std::runtime_error>(
                  ::testing::HasSubstr(expected)));
}

namespace {

constexpr std::array kInvalidDefinitions{
    std::make_pair("text"sv, "invalid libmagic category definition: text"sv),
    std::make_pair("text/*"sv, "invalid libmagic category definition"sv),
    std::make_pair("=text/*"sv, "empty category name"sv),
    std::make_pair("="sv, "empty category name"sv),
    std::make_pair("text="sv, "no patterns specified"sv),
    std::make_pair("text=:text/*"sv, "empty pattern"sv),
    std::make_pair("text=text/*::application/json"sv, "empty pattern"sv),
    std::make_pair("text=text/*:"sv, "empty pattern"sv),
};

} // namespace

INSTANTIATE_TEST_SUITE_P(libmagic_categorizer, invalid_definition_test,
                         ::testing::ValuesIn(kInvalidDefinitions));

using valid_definition_test =
    libmagic_categorizer_fixture<::testing::TestWithParam<std::string>>;

TEST_P(valid_definition_test, accepted) {
  auto const arg = fmt::format("--libmagic-category={}", GetParam());

  ASSERT_NO_THROW(create_catmgr({arg.c_str()}));
  EXPECT_TRUE(catmgr->category_value("cat").has_value());
}

INSTANTIATE_TEST_SUITE_P(libmagic_categorizer, valid_definition_test,
                         ::testing::Values("cat=text/plain", "cat=text/*",
                                           "cat=text/*:application/json",
                                           "cat=*/json",
                                           "cat=application/x-foo=bar"));
