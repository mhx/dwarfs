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

#include <cstring>
#include <exception>
#include <filesystem>
#include <random>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <folly/String.h>
#include <folly/lang/Bits.h>

#include <nlohmann/json.hpp>

#include <dwarfs/writer/categorizer.h>

#include "mmap_mock.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;
namespace po = boost::program_options;

template <typename Base>
class fits_categorizer_fixture : public Base {
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

    catmgr->add(catreg.create(lgr, "fits", vm, nullptr));
  }

 public:
  std::shared_ptr<writer::categorizer_manager> catmgr;
  test::test_logger lgr{logger::INFO};
};

using fits_categorizer = fits_categorizer_fixture<::testing::Test>;

TEST_F(fits_categorizer, requirements) {
  create_catmgr();

  EXPECT_THAT(
      [&] {
        auto cat = catmgr->category_value("fits/image").value();
        catmgr->set_metadata_requirements(cat, R"({"foo": ["set", ["bar"]]})");
      },
      ::testing::ThrowsMessage<std::runtime_error>(
          "unsupported metadata requirements: foo"));

  EXPECT_NO_THROW(catmgr->set_metadata_requirements(
      catmgr->category_value("fits/image").value(), R"({})"));
}

namespace {

constexpr std::string_view fits_header{
    // clang-format off
  // 0        1         2         3         4         5         6         7         8
  // 12345678901234567890123456789012345678901234567890123456789012345678901234567890
    "SIMPLE  =                    T / file does conform to FITS standard             "
    "BITPIX  =                   16 / number of bits per data pixel                  "
    "NAXIS   =                    2 / number of data axes                            "
    "NAXIS1  =                   16 / length of data axis 1                          "
    "NAXIS2  =                    8 / length of data axis 2                          "
    "EXTEND  =                    T / FITS dataset may contain extensions            "
    "END                                                                             "
    // clang-format on
};

void fill_fits_header(std::span<uint8_t> data) {
  std::memcpy(data.data(), fits_header.data(), fits_header.size());
  std::memset(data.data() + fits_header.size(), ' ', 2880 - fits_header.size());
}

} // namespace

TEST_F(fits_categorizer, unused_lsb_count_test) {
  create_catmgr();

  alignas(2) std::array<uint8_t, 2 * 2880 + 64> data;
  std::fill(data.begin(), data.end(), 0);

  auto metadata_category = catmgr->category_value("fits/metadata").value();
  auto image_category = catmgr->category_value("fits/image").value();

  std::map<writer::fragment_category, std::set<unsigned>> categories;

  for (size_t offset = 0; offset < 64; offset += 2) {
    std::span<uint8_t> fits{data.data() + offset, 2 * 2880};
    fill_fits_header(fits);
    std::span<uint16_t> image{reinterpret_cast<uint16_t*>(fits.data() + 2880),
                              8 * 16};
    for (auto& pixel : image) {
      for (unsigned unused_lsb_count = 0; unused_lsb_count <= 8;
           ++unused_lsb_count) {
        pixel = folly::Endian::big<uint16_t>(1 << unused_lsb_count);

        auto job = catmgr->job(
            fmt::format("test-{}-{}-{}", offset, pixel, unused_lsb_count));
        auto mm = test::make_mock_file_view(std::string{
            reinterpret_cast<char const*>(fits.data()), fits.size()});
        job.set_total_size(mm.size());
        job.categorize_random_access(mm);
        auto frag = job.result();
        auto fs = frag.span();
        ASSERT_EQ(3, fs.size());
        EXPECT_EQ(metadata_category, fs[0].category().value());
        EXPECT_EQ(2880, fs[0].size());
        EXPECT_EQ(image_category, fs[1].category().value());
        EXPECT_EQ(256, fs[1].size());
        EXPECT_EQ(metadata_category, fs[2].category().value());
        EXPECT_EQ(2624, fs[2].size());
        categories[fs[1].category()].insert(unused_lsb_count);
        pixel = 0;
      }
    }
  }

  EXPECT_EQ(9, categories.size());

  for (auto& [cat, unused_lsb_counts] : categories) {
    EXPECT_EQ(1, unused_lsb_counts.size());
    unsigned unused_lsb_count = *unused_lsb_counts.begin();
    auto json = catmgr->category_metadata(cat);
    auto metadata = nlohmann::json::parse(json);
    EXPECT_EQ(unused_lsb_count, metadata["unused_lsb_count"].get<int>());
  }
}
