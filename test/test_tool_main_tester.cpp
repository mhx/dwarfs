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

#include <dwarfs/string.h>
#include <dwarfs/util.h>

#include <dwarfs_tool_main.h>

#include "test_tool_main_tester.h"

namespace dwarfs::test {

namespace fs = std::filesystem;

namespace {

struct locale_setup_helper {
  locale_setup_helper() { setup_default_locale(); }
};

inline void setup_locale() { static locale_setup_helper helper; }

} // namespace

fs::path const test_dir = fs::path(TEST_DATA_DIR).make_preferred();
fs::path const audio_data_dir = test_dir / "pcmaudio";
fs::path const fits_data_dir = test_dir / "fits";

std::ostream& operator<<(std::ostream& os, input_mode m) {
  switch (m) {
  case input_mode::from_file:
    os << "from_file";
    break;
  case input_mode::from_stdin:
    os << "from_stdin";
    break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, path_type m) {
  switch (m) {
  case path_type::relative:
    os << "relative";
    break;
  case path_type::absolute:
    os << "absolute";
    break;
  case path_type::mixed:
    os << "mixed";
    break;
  }
  return os;
}

void tool_main_test::SetUp() {
  setup_locale();
  iol = std::make_unique<test::test_iolayer>();
}

void tool_main_test::TearDown() { iol.reset(); }

tester_common::tester_common(main_ptr_t mp, std::string toolname,
                             std::shared_ptr<test::os_access_mock> pos)
    : fa{std::make_shared<test::test_file_access>()}
    , os{std::move(pos)}
    , iol{std::make_unique<test::test_iolayer>(os, fa)}
    , main_{mp}
    , toolname_{std::move(toolname)} {
  setup_locale();
}

int tester_common::run(std::vector<std::string> args) {
  args.insert(args.begin(), toolname_);
  return tool::main_adapter(main_)(args, iol->get());
}

int tester_common::run(std::initializer_list<std::string> args) {
  return run(std::vector<std::string>(args));
}

int tester_common::run(std::string args) { return run(test::parse_args(args)); }

mkdwarfs_tester::mkdwarfs_tester(std::shared_ptr<test::os_access_mock> pos)
    : tester_common(tool::mkdwarfs_main, "mkdwarfs", std::move(pos)) {}

mkdwarfs_tester::mkdwarfs_tester()
    : mkdwarfs_tester(test::os_access_mock::create_test_instance()) {}

mkdwarfs_tester mkdwarfs_tester::create_empty() {
  return mkdwarfs_tester(std::make_shared<test::os_access_mock>());
}

void mkdwarfs_tester::add_stream_logger(std::ostream& st,
                                        logger::level_type level) {
  lgr = std::make_unique<stream_logger>(std::make_shared<test::test_terminal>(),
                                        st, logger_options{.threshold = level});
}

void mkdwarfs_tester::add_root_dir() {
  os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
}

void mkdwarfs_tester::add_special_files(bool with_regular_files) {
  if (with_regular_files) {
    static constexpr file_stat::off_type const size = 10;
    std::string data(size, 'x');
    os->add("suid", {1001, 0104755, 1, 0, 0, size, 0, 3333, 2222, 1111}, data);
    os->add("sgid", {1002, 0102755, 1, 0, 0, size, 0, 0, 0, 0}, data);
    os->add("sticky", {1003, 0101755, 1, 0, 0, size, 0, 0, 0, 0}, data);
  }
  os->add("block", {1004, 060666, 1, 0, 0, 0, 77, 0, 0, 0}, std::string{});
  os->add("sock", {1005, 0140666, 1, 0, 0, 0, 0, 0, 0, 0}, std::string{});
}

std::vector<std::pair<fs::path, std::string>>
mkdwarfs_tester::add_random_file_tree(random_file_tree_options const& opt) {
  size_t max_size{128 * static_cast<size_t>(opt.avg_size)};
  std::mt19937_64 rng{42};
  std::exponential_distribution<> size_dist{1 / opt.avg_size};
  std::uniform_int_distribution<> path_comp_size_dist{0, opt.max_name_len};
  std::uniform_int_distribution<> invalid_dist{0, 1};
  std::vector<std::pair<fs::path, std::string>> paths;

  auto random_path_component = [&] {
    auto size = path_comp_size_dist(rng);
    if (opt.with_invalid_utf8 && invalid_dist(rng) == 0) {
      return test::create_random_string(size, 96, 255, rng);
    }
    return test::create_random_string(size, 'A', 'Z', rng);
  };

  test::lz_params text_lzp{};
  test::lz_params binary_lzp{};
  text_lzp.text_mode = true;
  binary_lzp.text_mode = false;
  text_lzp.seed = rng();
  binary_lzp.seed = rng();
  test::lz_synthetic_generator text_gen{text_lzp};
  test::lz_synthetic_generator binary_gen{binary_lzp};

  for (int x = 0; x < opt.dimension; ++x) {
    fs::path d1{random_path_component() + std::to_string(x)};
    os->add_dir(d1);

    for (int y = 0; y < opt.dimension; ++y) {
      fs::path d2{d1 / (random_path_component() + std::to_string(y))};
      os->add_dir(d2);

      for (int z = 0; z < opt.dimension; ++z) {
        fs::path f{d2 / (random_path_component() + std::to_string(z))};
        auto const size =
            std::min(max_size, static_cast<size_t>(size_dist(rng)));
        std::string data;

        auto const choice = rng() % 4;
        switch (choice) {
        case 0:
          data = test::create_random_string(size, rng);
          break;
        case 1:
          data = test::loremipsum(size);
          break;
        case 3:
          data = text_gen.generate(size);
          break;
        case 4:
          data = binary_gen.generate(size);
          break;
        }

        os->add_file(f, data);
        paths.emplace_back(f, data);

        if (opt.with_errors) {
          auto failpath = fs::path{"/"} / f;
          switch (rng() % 8) {
          case 0:
            os->set_access_fail(failpath);
            [[fallthrough]];
          case 1:
          case 2:
            os->set_map_file_error(
                failpath,
                std::make_exception_ptr(std::runtime_error("map_file_error")),
                rng() % 4);
            break;

          default:
            break;
          }
        }
      }
    }
  }

  return paths;
}

void mkdwarfs_tester::add_test_file_tree(bool with_regular_files) {
  for (auto const& [stat, name] : test::test_dirtree()) {
    auto path = name.substr(name.size() == 5 ? 5 : 6);

    switch (stat.type()) {
    case posix_file_type::regular:
      if (with_regular_files) {
        os->add(path, stat,
                [size = stat.size] { return test::loremipsum(size); });
      }
      break;
    case posix_file_type::symlink:
      os->add(path, stat, test::loremipsum(stat.size));
      break;
    default:
      os->add(path, stat);
      break;
    }
  }
}

reader::filesystem_v2
mkdwarfs_tester::fs_from_data(std::string data,
                              reader::filesystem_options const& opt) {
  if (!lgr) {
    lgr = std::make_unique<test::test_logger>();
  }
  auto mm = test::make_mock_file_view(std::move(data));
  return reader::filesystem_v2(*lgr, *os, mm, opt);
}

reader::filesystem_v2
mkdwarfs_tester::fs_from_file(std::string path,
                              reader::filesystem_options const& opt) {
  auto fsimage = fa->get_file(path);
  if (!fsimage) {
    throw std::runtime_error("file not found: " + path);
  }
  return fs_from_data(std::move(fsimage.value()), opt);
}

reader::filesystem_v2
mkdwarfs_tester::fs_from_stdout(reader::filesystem_options const& opt) {
  return fs_from_data(out(), opt);
}

dwarfsck_tester::dwarfsck_tester(std::shared_ptr<test::os_access_mock> pos)
    : tester_common(tool::dwarfsck_main, "dwarfsck", std::move(pos)) {}

dwarfsck_tester::dwarfsck_tester()
    : dwarfsck_tester(std::make_shared<test::os_access_mock>()) {}

dwarfsck_tester dwarfsck_tester::create_with_image(std::string image) {
  auto os = std::make_shared<test::os_access_mock>();
  os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  os->add_file("image.dwarfs", std::move(image));
  return dwarfsck_tester(std::move(os));
}

dwarfsck_tester dwarfsck_tester::create_with_image() {
  return create_with_image(build_test_image());
}

dwarfsextract_tester::dwarfsextract_tester(
    std::shared_ptr<test::os_access_mock> pos)
    : tester_common(tool::dwarfsextract_main, "dwarfsextract", std::move(pos)) {
}

dwarfsextract_tester::dwarfsextract_tester()
    : dwarfsextract_tester(std::make_shared<test::os_access_mock>()) {}

dwarfsextract_tester
dwarfsextract_tester::create_with_image(std::string image) {
  auto os = std::make_shared<test::os_access_mock>();
  os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  os->add_file("image.dwarfs", std::move(image));
  return dwarfsextract_tester(std::move(os));
}

dwarfsextract_tester dwarfsextract_tester::create_with_image() {
  return create_with_image(build_test_image());
}

int mkdwarfs_main_test::run(std::vector<std::string> args) {
  args.insert(args.begin(), "mkdwarfs");
  return tool::main_adapter(tool::mkdwarfs_main)(args, iol->get());
}

int dwarfsck_main_test::run(std::vector<std::string> args) {
  args.insert(args.begin(), "dwarfsck");
  return tool::main_adapter(tool::dwarfsck_main)(args, iol->get());
}

int dwarfsextract_main_test::run(std::vector<std::string> args) {
  args.insert(args.begin(), "dwarfsextract");
  return tool::main_adapter(tool::dwarfsextract_main)(args, iol->get());
}

std::string build_test_image(std::vector<std::string> extra_args,
                             std::map<std::string, std::string> extra_files) {
  mkdwarfs_tester t;
  for (auto const& [name, contents] : extra_files) {
    t.fa->set_file(name, contents);
  }
  std::vector<std::string> args = {"-i", "/", "-o", "-"};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  if (t.run(args) != 0) {
    throw std::runtime_error("failed to build test image:\n" + t.err());
  }
  return t.out();
}

std::tuple<std::optional<reader::filesystem_v2>, mkdwarfs_tester>
build_with_args(std::vector<std::string> opt_args) {
  std::string const image_file = "test.dwarfs";
  mkdwarfs_tester t;
  std::vector<std::string> args = {"-i", "/", "-o", image_file};
  args.insert(args.end(), opt_args.begin(), opt_args.end());
  if (t.run(args) != 0) {
    return {std::nullopt, std::move(t)};
  }
  return {t.fs_from_file(image_file), std::move(t)};
}

std::set<uint64_t> get_all_fs_times(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> times;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    times.insert(st.atime());
    times.insert(st.ctime());
    times.insert(st.mtime());
  });
  return times;
}

std::set<uint64_t> get_all_fs_uids(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> uids;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    uids.insert(st.uid());
  });
  return uids;
}

std::set<uint64_t> get_all_fs_gids(reader::filesystem_v2 const& fs) {
  std::set<uint64_t> gids;
  fs.walk([&](auto const& e) {
    auto st = fs.getattr(e.inode());
    gids.insert(st.gid());
  });
  return gids;
}

std::unordered_map<std::string, std::string>
get_md5_checksums(std::string image) {
  auto os = std::make_shared<test::os_access_mock>();
  os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  os->add_file("image.dwarfs", std::move(image));
  auto t = dwarfsck_tester(std::move(os));
  if (t.run({"image.dwarfs", "--checksum=md5"}) != 0) {
    throw std::runtime_error("Failed to run dwarfsck: " + t.err());
  }
  auto out = t.out();

  std::unordered_map<std::string, std::string> checksums;

  for (auto line : split_to<std::vector<std::string_view>>(out, '\n')) {
    if (line.empty()) {
      continue;
    }
    auto pos = line.find("  ");
    if (pos == std::string::npos) {
      throw std::runtime_error("Invalid checksum line: " + std::string(line));
    }
    auto hash = line.substr(0, pos);
    auto file = line.substr(pos + 2);
    checksums.emplace(file, hash);
  }

  return checksums;
};

} // namespace dwarfs::test
