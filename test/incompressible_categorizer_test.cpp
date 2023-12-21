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

#include <cstring>
#include <exception>
#include <filesystem>
#include <random>
#include <vector>

// #include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <folly/String.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/mmap.h"

#include "loremipsum.h"
#include "test_logger.h"

using namespace dwarfs;
using dwarfs::test::loremipsum;
// using testing::MatchesRegex;

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

std::string random_string(size_t size) {
  using random_bytes_engine =
      std::independent_bits_engine<std::default_random_engine, CHAR_BIT,
                                   unsigned short>;

  static random_bytes_engine rbe;

  std::string data;
  data.resize(size);
  std::generate(begin(data), end(data), std::ref(rbe));

  return data;
}

std::vector<uint8_t> make_data(std::string const& s) {
  std::vector<uint8_t> rv(s.size());
  std::memcpy(rv.data(), s.data(), s.size());
  return rv;
}

} // namespace

template <typename Base>
class incompressible_categorizer_fixture : public Base {
 protected:
  void SetUp() override { lgr.clear(); }

  void create_catmgr() { create_catmgr({}); }

  void create_catmgr(std::vector<char const*> args) {
    auto& catreg = categorizer_registry::instance();

    po::options_description opts;
    catreg.add_options(opts);

    args.insert(args.begin(), "program");

    po::variables_map vm;
    auto parsed = po::parse_command_line(args.size(), args.data(), opts);

    po::store(parsed, vm);
    po::notify(vm);

    catmgr = std::make_shared<categorizer_manager>(lgr);

    catmgr->add(catreg.create(lgr, "incompressible", vm));
  }

  // void TearDown() override {
  // }

 public:
  auto categorize(fs::path const& path, std::span<uint8_t const> data) {
    auto job = catmgr->job(path);
    job.set_total_size(data.size());
    job.categorize_random_access(data);
    job.categorize_sequential(data);
    return job.result();
  }

  std::shared_ptr<categorizer_manager> catmgr;
  test::test_logger lgr{logger::INFO};
};

using incompressible_categorizer =
    incompressible_categorizer_fixture<::testing::Test>;

TEST_F(incompressible_categorizer, requirements) {
  create_catmgr();
  try {
    catmgr->set_metadata_requirements(
        catmgr->category_value("incompressible").value(),
        R"({"foo": ["set", ["bar"]]})");
    FAIL() << "expected std::runtime_error";
  } catch (std::runtime_error const& e) {
    EXPECT_STREQ("unsupported metadata requirements: foo", e.what());
  } catch (...) {
    FAIL() << "unexpected exception: "
           << folly::exceptionStr(std::current_exception());
  }

  catmgr->set_metadata_requirements(
      catmgr->category_value("incompressible").value(), R"({})");
}

TEST_F(incompressible_categorizer, categorize_incompressible) {
  create_catmgr();

  auto data = make_data(random_string(10'000));
  auto frag = categorize("random.txt", data);
  ASSERT_EQ(1, frag.size());
  EXPECT_EQ("incompressible",
            catmgr->category_name(frag.get_single_category().value()));
}

TEST_F(incompressible_categorizer, categorize_default) {
  create_catmgr();

  auto data = make_data(loremipsum(10'000));
  auto frag = categorize("ipsum.txt", data);
  EXPECT_TRUE(frag.empty());
}

TEST_F(incompressible_categorizer, categorize_fragments) {
  create_catmgr(
      {"--incompressible-block-size=8k", "--incompressible-fragments"});

  // data:  CCCCCCCCCCCCIIIIIIIIIIIICCCCCCCCCCCCIIIIIIIIIIIICCC
  // block: 0-------1-------2-------3-------4-------5-------6--
  // frag:  def-------------incomp--def-------------incomp--def
  auto data = make_data(loremipsum(12 * 1024) + random_string(12 * 1024) +
                        loremipsum(12 * 1024) + random_string(12 * 1024) +
                        loremipsum(3 * 1024));

  auto frag = categorize("mixed.txt", data);
  ASSERT_EQ(5, frag.size());

  std::vector<std::pair<std::string_view, size_t>> ref{
      {"<default>", 16384},     {"incompressible", 8192}, {"<default>", 16384},
      {"incompressible", 8192}, {"<default>", 3072},
  };

  for (size_t i = 0; i < ref.size(); ++i) {
    auto const& r = ref[i];
    auto const& f = frag.span()[i];

    EXPECT_EQ(r.first, catmgr->category_name(f.category().value())) << i;
    EXPECT_EQ(r.second, f.length()) << i;
  }
}

TEST_F(incompressible_categorizer, min_input_size) {
  create_catmgr({"--incompressible-min-input-size=1000"});

  {
    auto data = make_data(random_string(999));
    auto frag = categorize("random.txt", data);
    EXPECT_TRUE(frag.empty());
  }
  {
    auto data = make_data(random_string(10'000));
    auto frag = categorize("random.txt", data);
    ASSERT_EQ(1, frag.size());
    EXPECT_EQ("incompressible",
              catmgr->category_name(frag.get_single_category().value()));
  }
}

using max_ratio_test = incompressible_categorizer_fixture<
    ::testing::TestWithParam<std::pair<double, bool>>>;

TEST_P(max_ratio_test, max_ratio) {
  auto [ratio, is_incompressible] = GetParam();
  auto arg = fmt::format("--incompressible-ratio={:f}", ratio);

  create_catmgr({arg.c_str()});

  auto data = make_data(loremipsum(10'000));
  auto frag = categorize("ipsum.txt", data);
  if (is_incompressible) {
    ASSERT_EQ(1, frag.size());
    EXPECT_EQ("incompressible",
              catmgr->category_name(frag.get_single_category().value()));
  } else {
    EXPECT_TRUE(frag.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(incompressible_categorizer, max_ratio_test,
                         ::testing::Values(std::make_pair(0.4, true),
                                           std::make_pair(0.6, false)));

using zstd_accel_test = incompressible_categorizer_fixture<
    ::testing::TestWithParam<std::pair<int, bool>>>;

TEST_P(zstd_accel_test, zstd_acceleration) {
  auto [accel, is_incompressible] = GetParam();
  auto arg = fmt::format("--incompressible-zstd-level={}", accel);

  create_catmgr({arg.c_str()});

  auto data = make_data(loremipsum(10'000));
  auto frag = categorize("ipsum.txt", data);
  if (is_incompressible) {
    ASSERT_EQ(1, frag.size());
    EXPECT_EQ("incompressible",
              catmgr->category_name(frag.get_single_category().value()));
  } else {
    EXPECT_TRUE(frag.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(incompressible_categorizer, zstd_accel_test,
                         ::testing::Values(std::make_pair(-1, false),
                                           std::make_pair(-10, false),
                                           std::make_pair(-100, true)));
