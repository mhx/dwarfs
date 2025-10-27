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

#include <array>
#include <filesystem>
#include <initializer_list>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <dwarfs/config.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/tool/main_adapter.h>

#include "loremipsum.h"
#include "lz_synthetic_generator.h"
#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

namespace dwarfs::test {

// TODO: this is a workaround for older Clang versions
struct fs_path_hash {
  auto operator()(std::filesystem::path const& p) const noexcept {
    return std::filesystem::hash_value(p);
  }
};

extern std::filesystem::path const test_dir;
extern std::filesystem::path const audio_data_dir;
extern std::filesystem::path const fits_data_dir;
extern std::filesystem::path const binary_data_dir;

constexpr std::array<std::string_view, 6> const log_level_strings{
    "error", "warn", "info", "verbose", "debug", "trace"};

enum class input_mode {
  from_file,
  from_stdin,
};

constexpr std::array input_modes{
    input_mode::from_file,
    input_mode::from_stdin,
};

std::ostream& operator<<(std::ostream& os, input_mode m);

enum class path_type {
  relative,
  absolute,
  mixed,
};

constexpr std::array path_types{path_type::relative, path_type::absolute,
                                path_type::mixed};

std::ostream& operator<<(std::ostream& os, path_type m);

class tester_common {
 public:
  using main_ptr_t = tool::main_adapter::main_fn_type;

  tester_common(main_ptr_t mp, std::string toolname,
                std::shared_ptr<test::os_access_mock> pos);

  int run(std::vector<std::string> args);
  int run(std::initializer_list<std::string> args);
  int run(std::string args);

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::shared_ptr<test::test_file_access> fa;
  std::shared_ptr<test::os_access_mock> os;
  std::unique_ptr<test::test_iolayer> iol;

 private:
  main_ptr_t main_;
  std::string toolname_;
};

struct random_file_tree_options {
  double avg_size{4096.0};
  size_t min_size{0};
  int dimension{20};
  int max_name_len{50};
  bool with_errors{false};
  bool with_invalid_utf8{false};
  bool only_random_contents{false};
};

constexpr auto default_fs_opts = reader::filesystem_options{
    .block_cache = {.max_bytes = 256 * 1024,
                    .sequential_access_detector_threshold = 4},
    .metadata = {.check_consistency = true}};

class mkdwarfs_tester : public tester_common {
 public:
  mkdwarfs_tester();
  explicit mkdwarfs_tester(std::shared_ptr<test::os_access_mock> pos);

  static mkdwarfs_tester create_empty();

  void add_stream_logger(std::ostream& st,
                         logger::level_type level = logger::VERBOSE);

  void add_root_dir();
  void add_special_files(bool with_regular_files = true);
  void add_test_file_tree(bool with_regular_files = true);

  std::vector<std::pair<std::filesystem::path, std::string>>
  add_random_file_tree(
      random_file_tree_options const& opt = random_file_tree_options{});

  reader::filesystem_v2
  fs_from_data(std::string data,
               reader::filesystem_options const& opt = default_fs_opts);

  reader::filesystem_v2
  fs_from_file(std::string path,
               reader::filesystem_options const& opt = default_fs_opts);

  reader::filesystem_v2
  fs_from_stdout(reader::filesystem_options const& opt = default_fs_opts);

  std::unique_ptr<logger> lgr;
};

std::string
build_test_image(std::vector<std::string> extra_args = {},
                 std::map<std::string, std::string> extra_files = {});

class dwarfsck_tester : public tester_common {
 public:
  dwarfsck_tester();
  explicit dwarfsck_tester(std::shared_ptr<test::os_access_mock> pos);

  static dwarfsck_tester create_with_image();
  static dwarfsck_tester create_with_image(std::string image);
};

class dwarfsextract_tester : public tester_common {
 public:
  dwarfsextract_tester();
  explicit dwarfsextract_tester(std::shared_ptr<test::os_access_mock> pos);

  static dwarfsextract_tester create_with_image();
  static dwarfsextract_tester create_with_image(std::string image);
};

std::tuple<std::optional<reader::filesystem_v2>, mkdwarfs_tester>
build_with_args(std::vector<std::string> opt_args = {});

std::set<uint64_t> get_all_fs_times(reader::filesystem_v2 const& fs);
std::set<uint64_t> get_all_fs_uids(reader::filesystem_v2 const& fs);
std::set<uint64_t> get_all_fs_gids(reader::filesystem_v2 const& fs);

std::unordered_map<std::string, std::string>
get_md5_checksums(std::string image);

class tool_main_test : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::unique_ptr<test::test_iolayer> iol;
};

class mkdwarfs_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args);
};

class dwarfsck_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args);
};

class dwarfsextract_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args);
};

} // namespace dwarfs::test
