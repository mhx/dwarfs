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

#include <dwarfs/writer/categorizer.h>

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
