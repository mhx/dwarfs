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

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <nlohmann/json.hpp>

#include <dwarfs/file_util.h>
#include <dwarfs/writer/categorizer.h>

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

fs::path const test_dir = fs::path(TEST_DATA_DIR).make_preferred();
fs::path const binary_data_dir = test_dir / "binary";

} // namespace

class binary_categorizer : public ::testing::Test {
 protected:
  void SetUp() override { create_catmgr(); }

  void TearDown() override {
    lgr.clear();
    catmgr.reset();
  }

  void create_catmgr() {
    writer::categorizer_registry catreg;

    po::options_description opts;
    catreg.add_options(opts);

    std::vector<char const*> args{"program"};

    po::variables_map vm;
    auto parsed = po::parse_command_line(args.size(), args.data(), opts);

    po::store(parsed, vm);
    po::notify(vm);

    catmgr = std::make_shared<writer::categorizer_manager>(lgr, "/");

    catmgr->add(catreg.create(lgr, "binary", vm, nullptr));
  }

 public:
  std::shared_ptr<writer::categorizer_manager> catmgr;
  test::test_logger lgr{logger::INFO};
};

TEST_F(binary_categorizer, elf_basic) {
  auto elf_category = catmgr->category_value("binary/elf").value();

  auto job_aarch64 = catmgr->job("elf-aarch64");
  auto mm_aarch64 = test::make_real_file_view(binary_data_dir / "elf-aarch64");
  job_aarch64.set_total_size(mm_aarch64.size());
  job_aarch64.categorize_random_access(mm_aarch64);
  auto frag_aarch64 = job_aarch64.result();
  EXPECT_EQ(1, frag_aarch64.size());
  EXPECT_EQ(mm_aarch64.size(), frag_aarch64.total_size());
  EXPECT_EQ(elf_category, frag_aarch64.get_single_category().value());

  auto job_i386 = catmgr->job("elf-i386");
  auto mm_i386 = test::make_real_file_view(binary_data_dir / "elf-i386");
  job_i386.set_total_size(mm_i386.size());
  job_i386.categorize_random_access(mm_i386);
  auto frag_i386 = job_i386.result();
  EXPECT_EQ(1, frag_i386.size());
  EXPECT_EQ(mm_i386.size(), frag_i386.total_size());
  EXPECT_EQ(elf_category, frag_i386.get_single_category().value());

  EXPECT_NE(frag_aarch64.get_single_category().subcategory(),
            frag_i386.get_single_category().subcategory());
}

TEST_F(binary_categorizer, elf_fail) {
  auto data = read_file(binary_data_dir / "elf-aarch64");

  {
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(data.substr(0, 63));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }

  {
    auto job = catmgr->job("success");
    auto mm = test::make_mock_file_view(data.substr(0, 64));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(1, frag.size());
  }

  {
    auto corrupted = data;
    corrupted[0] = 0x00;
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(corrupted);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }
}

TEST_F(binary_categorizer, pe_basic) {
  auto pe_category = catmgr->category_value("binary/pe").value();

  auto job = catmgr->job("pe-amd64");
  auto mm = test::make_real_file_view(binary_data_dir / "pe-amd64");
  job.set_total_size(mm.size());
  job.categorize_random_access(mm);
  auto frag = job.result();
  EXPECT_EQ(1, frag.size());
  EXPECT_EQ(mm.size(), frag.total_size());
  EXPECT_EQ(pe_category, frag.get_single_category().value());
}

TEST_F(binary_categorizer, pe_fail) {
  auto data = read_file(binary_data_dir / "pe-amd64");

  {
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(data.substr(0, 63));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }

  {
    auto job = catmgr->job("success");
    auto mm = test::make_mock_file_view(data.substr(0, 1024));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(1, frag.size());
  }

  {
    auto corrupted = data;
    corrupted[0] = 0x00;
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(corrupted);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }
}

TEST_F(binary_categorizer, macho_basic_thin) {
  auto macho_category = catmgr->category_value("binary/macho-section").value();

  auto job_arm64 = catmgr->job("macho-arm64");
  auto mm_arm64 = test::make_real_file_view(binary_data_dir / "macho-arm64");
  job_arm64.set_total_size(mm_arm64.size());
  job_arm64.categorize_random_access(mm_arm64);
  auto frag_arm64 = job_arm64.result();
  EXPECT_EQ(1, frag_arm64.size());
  EXPECT_EQ(mm_arm64.size(), frag_arm64.total_size());
  EXPECT_EQ(macho_category, frag_arm64.get_single_category().value());

  auto job_x86_64 = catmgr->job("macho-x86_64");
  auto mm_x86_64 = test::make_real_file_view(binary_data_dir / "macho-x86_64");
  job_x86_64.set_total_size(mm_x86_64.size());
  job_x86_64.categorize_random_access(mm_x86_64);
  auto frag_x86_64 = job_x86_64.result();
  EXPECT_EQ(1, frag_x86_64.size());
  EXPECT_EQ(mm_x86_64.size(), frag_x86_64.total_size());
  EXPECT_EQ(macho_category, frag_x86_64.get_single_category().value());

  EXPECT_NE(frag_arm64.get_single_category().subcategory(),
            frag_x86_64.get_single_category().subcategory());
}

TEST_F(binary_categorizer, macho_fail_thin) {
  auto data = read_file(binary_data_dir / "macho-arm64");

  {
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(data.substr(0, 63));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }

  {
    auto job = catmgr->job("success");
    auto mm = test::make_mock_file_view(data.substr(0, 64));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(1, frag.size());
  }

  {
    auto corrupted = data;
    corrupted[0] = 0x00;
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(corrupted);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }
}

TEST_F(binary_categorizer, macho_basic_fat) {
  auto header_category = catmgr->category_value("binary/macho-header").value();
  auto macho_category = catmgr->category_value("binary/macho-section").value();

  auto job_arm64 = catmgr->job("macho-arm64");
  auto mm_arm64 = test::make_real_file_view(binary_data_dir / "macho-arm64");
  job_arm64.set_total_size(mm_arm64.size());
  job_arm64.categorize_random_access(mm_arm64);
  auto frag_arm64 = job_arm64.result();
  EXPECT_EQ(1, frag_arm64.size());
  EXPECT_EQ(mm_arm64.size(), frag_arm64.total_size());
  EXPECT_EQ(macho_category, frag_arm64.get_single_category().value());

  auto job_x86_64 = catmgr->job("macho-x86_64");
  auto mm_x86_64 = test::make_real_file_view(binary_data_dir / "macho-x86_64");
  job_x86_64.set_total_size(mm_x86_64.size());
  job_x86_64.categorize_random_access(mm_x86_64);
  auto frag_x86_64 = job_x86_64.result();
  EXPECT_EQ(1, frag_x86_64.size());
  EXPECT_EQ(mm_x86_64.size(), frag_x86_64.total_size());
  EXPECT_EQ(macho_category, frag_x86_64.get_single_category().value());

  EXPECT_NE(frag_arm64.get_single_category().subcategory(),
            frag_x86_64.get_single_category().subcategory());

  auto job_fat32 = catmgr->job("macho-arm64-x86_64");
  auto mm_fat32 =
      test::make_real_file_view(binary_data_dir / "macho-arm64-x86_64");
  job_fat32.set_total_size(mm_fat32.size());
  job_fat32.categorize_random_access(mm_fat32);
  auto frag_fat32 = job_fat32.result();
  ASSERT_EQ(4, frag_fat32.size());
  auto span_fat32 = frag_fat32.span();
  EXPECT_EQ(header_category, span_fat32[0].category().value());
  EXPECT_EQ(macho_category, span_fat32[1].category().value());
  EXPECT_EQ(header_category, span_fat32[2].category().value());
  EXPECT_EQ(macho_category, span_fat32[3].category().value());
  EXPECT_NE(span_fat32[1].category().subcategory(),
            span_fat32[3].category().subcategory());
  EXPECT_EQ(span_fat32[1].category().subcategory(),
            frag_x86_64.get_single_category().subcategory());
  EXPECT_EQ(span_fat32[3].category().subcategory(),
            frag_arm64.get_single_category().subcategory());
  EXPECT_EQ(frag_fat32.total_size(), mm_fat32.size());
  EXPECT_EQ(4096, span_fat32[0].size());
  EXPECT_EQ(mm_x86_64.size(), span_fat32[1].size());
  EXPECT_EQ(8016, span_fat32[2].size());
  EXPECT_EQ(mm_arm64.size(), span_fat32[3].size());

  auto job_fat64 = catmgr->job("macho-fat64-arm64-x86_64");
  auto mm_fat64 =
      test::make_real_file_view(binary_data_dir / "macho-fat64-arm64-x86_64");
  job_fat64.set_total_size(mm_fat64.size());
  job_fat64.categorize_random_access(mm_fat64);
  auto frag_fat64 = job_fat64.result();
  ASSERT_EQ(4, frag_fat64.size());
  auto span_fat64 = frag_fat64.span();
  EXPECT_EQ(header_category, span_fat64[0].category().value());
  EXPECT_EQ(macho_category, span_fat64[1].category().value());
  EXPECT_EQ(header_category, span_fat64[2].category().value());
  EXPECT_EQ(macho_category, span_fat64[3].category().value());
  EXPECT_NE(span_fat64[1].category().subcategory(),
            span_fat64[3].category().subcategory());
  EXPECT_EQ(span_fat64[1].category().subcategory(),
            frag_x86_64.get_single_category().subcategory());
  EXPECT_EQ(span_fat64[3].category().subcategory(),
            frag_arm64.get_single_category().subcategory());
  EXPECT_EQ(frag_fat64.total_size(), mm_fat64.size());
  EXPECT_EQ(4096, span_fat64[0].size());
  EXPECT_EQ(mm_x86_64.size(), span_fat64[1].size());
  EXPECT_EQ(8016, span_fat64[2].size());
  EXPECT_EQ(mm_arm64.size(), span_fat64[3].size());
}

TEST_F(binary_categorizer, macho_fail_fat) {
  auto data = read_file(binary_data_dir / "macho-arm64-x86_64");

  {
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(data.substr(0, 63));
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size());
  }

  {
    auto job = catmgr->job("success");
    auto mm = test::make_mock_file_view(data);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(4, frag.size());
  }

  std::array significant_bytes{
      0x0000, // fat header magic
      0x0007, // number of architectures
      0x0010, // first architecture offset
      0x0028, // second architecture size
      0x1001, // first architecture macho magic
      0x4002, // second architecture macho magic
  };

  for (auto const idx : significant_bytes) {
    auto corrupted = data;
    corrupted[idx] = 0xFF;
    auto job = catmgr->job("fail");
    auto mm = test::make_mock_file_view(corrupted);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto frag = job.result();
    EXPECT_EQ(0, frag.size()) << "for corrupted byte index " << idx << "\n"
                              << frag.to_string();
  }
}
