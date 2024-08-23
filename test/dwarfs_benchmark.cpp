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

#include <fstream>
#include <sstream>

#include <benchmark/benchmark.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <dwarfs/block_compressor.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/reader/getattr_options.h>
#include <dwarfs/reader/iovec_read_buf.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/vfs_stat.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/internal/string_table.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"
#include "test_strings.h"

namespace {

using namespace dwarfs;

void PackParams(::benchmark::internal::Benchmark* b) {
  for (auto pack_directories : {false, true}) {
    for (auto plain_tables : {false, true}) {
      if (plain_tables) {
        b->Args({pack_directories, true, false, false});
      } else {
        for (auto pack_strings : {false, true}) {
          for (auto pack_strings_index : {false, true}) {
            b->Args(
                {pack_directories, false, pack_strings, pack_strings_index});
          }
        }
      }
    }
  }
}

void PackParamsStrings(::benchmark::internal::Benchmark* b) {
  for (auto plain_tables : {false, true}) {
    if (plain_tables) {
      b->Args({true, true, false, false});
    } else {
      for (auto pack_strings : {false, true}) {
        for (auto pack_strings_index : {false, true}) {
          b->Args({true, false, pack_strings, pack_strings_index});
        }
      }
    }
  }
}

void PackParamsNone(::benchmark::internal::Benchmark* b) {
  b->Args({true, false, true, true});
}

void PackParamsDirs(::benchmark::internal::Benchmark* b) {
  for (auto pack_directories : {false, true}) {
    b->Args({pack_directories, false, true, true});
  }
}

std::string
make_filesystem(::benchmark::State const* state,
                std::shared_ptr<os_access> os = nullptr,
                writer::segmenter_factory::config const* segcfg = nullptr) {
  writer::segmenter_factory::config cfg;
  writer::scanner_options options;

  cfg.blockhash_window_size.set_default(12);
  cfg.window_increment_shift.set_default(1);
  cfg.max_active_blocks.set_default(1);
  cfg.bloom_filter_size.set_default(4);
  cfg.block_size_bits = 12;

  options.inode.fragment_order.set_default(
      {.mode = writer::fragment_order_mode::PATH});

  options.with_devices = true;
  options.with_specials = true;
  options.keep_all_times = false;
  options.pack_chunk_table = true;
  options.pack_directories = state ? state->range(0) : true;
  options.pack_shared_files_table = true;
  options.pack_names = state ? state->range(2) : true;
  options.pack_names_index = state ? state->range(3) : true;
  options.pack_symlinks = state ? state->range(2) : true;
  options.pack_symlinks_index = state ? state->range(3) : true;
  options.force_pack_string_tables = true;
  options.plain_names_table = state ? state->range(1) : false;
  options.plain_symlinks_table = state ? state->range(1) : false;

  test::test_logger lgr;

  if (!os) {
    os = test::os_access_mock::create_test_instance();
  }

  thread_pool pool(lgr, *os, "writer", 4);
  writer::writer_progress prog;

  writer::segmenter_factory sf(lgr, prog, segcfg ? *segcfg : cfg);
  writer::entry_factory ef;

  writer::scanner s(lgr, pool, sf, ef, *os, options);

  std::ostringstream oss;

  block_compressor bc("null");
  writer::filesystem_writer fsw(oss, lgr, pool, prog);
  fsw.add_default_compressor(bc);

  s.scan(fsw, "", prog);

  return oss.str();
}

template <typename T>
auto make_frozen_string_table(
    T const& strings, internal::string_table::pack_options const& options) {
  using namespace apache::thrift::frozen;
  std::string tmp;
  auto tbl = internal::string_table::pack(strings, options);
  freezeToString(tbl, tmp);
  return mapFrozen<thrift::metadata::string_table>(std::move(tmp));
}

auto make_frozen_legacy_string_table(std::vector<std::string>&& strings) {
  using namespace apache::thrift::frozen;
  std::string tmp;
  freezeToString(strings, tmp);
  return mapFrozen<std::vector<std::string>>(std::move(tmp));
}

void frozen_legacy_string_table_lookup(::benchmark::State& state) {
  auto data = make_frozen_legacy_string_table(test::test_string_vector());
  internal::string_table table(data);
  int i = 0;
  std::string str;

  for (auto _ : state) {
    ::benchmark::DoNotOptimize(str = table[i++ % test::NUM_STRINGS]);
  }
}

void frozen_string_table_lookup(::benchmark::State& state) {
  auto data = make_frozen_string_table(
      test::test_strings, internal::string_table::pack_options(
                              state.range(0), state.range(1), true));
  test::test_logger lgr;
  internal::string_table table(lgr, "bench", data);
  int i = 0;
  std::string str;

  for (auto _ : state) {
    ::benchmark::DoNotOptimize(str = table[i++ % test::NUM_STRINGS]);
  }
}

void dwarfs_initialize(::benchmark::State& state) {
  auto image = make_filesystem(&state);
  test::test_logger lgr;
  test::os_access_mock os;
  auto mm = std::make_shared<test::mmap_mock>(image);
  reader::filesystem_options opts;
  opts.block_cache.max_bytes = 1 << 20;
  opts.metadata.enable_nlink = true;

  for (auto _ : state) {
    auto fs = reader::filesystem_v2(lgr, os, mm, opts);
    ::benchmark::DoNotOptimize(fs);
  }
}

class filesystem : public ::benchmark::Fixture {
 public:
  static constexpr size_t NUM_ENTRIES = 8;

  void SetUp(::benchmark::State const& state) {
    image = make_filesystem(&state);
    mm = std::make_shared<test::mmap_mock>(image);
    reader::filesystem_options opts;
    opts.block_cache.max_bytes = 1 << 20;
    opts.metadata.enable_nlink = true;
    fs = std::make_unique<reader::filesystem_v2>(lgr, os, mm, opts);
    inode_views.reserve(NUM_ENTRIES);
    for (int i = 0; inode_views.size() < NUM_ENTRIES; ++i) {
      if (auto iv = fs->find(i)) {
        inode_views.emplace_back(*iv);
      }
    }
  }

  void TearDown(::benchmark::State const&) {
    inode_views.clear();
    image.clear();
    mm.reset();
    fs.reset();
  }

  void read_bench(::benchmark::State& state, const char* file) {
    auto dev = fs->find(file);
    auto iv = dev->inode();
    auto st = fs->getattr(iv);
    auto i = fs->open(iv);
    std::string buf;
    auto size = st.size();
    buf.resize(size);

    for (auto _ : state) {
      auto r = fs->read(i, buf.data(), size);
      ::benchmark::DoNotOptimize(r);
    }
  }

  void read_string_bench(::benchmark::State& state, const char* file) {
    auto dev = fs->find(file);
    auto i = fs->open(dev->inode());

    for (auto _ : state) {
      auto r = fs->read_string(i);
      ::benchmark::DoNotOptimize(r);
    }
  }

  void readv_bench(::benchmark::State& state, char const* file) {
    auto dev = fs->find(file);
    auto i = fs->open(dev->inode());

    for (auto _ : state) {
      reader::iovec_read_buf buf;
      auto r = fs->readv(i, buf);
      ::benchmark::DoNotOptimize(r);
    }
  }

  void readv_future_bench(::benchmark::State& state, char const* file) {
    auto dev = fs->find(file);
    auto i = fs->open(dev->inode());

    for (auto _ : state) {
      auto x = fs->readv(i);
      for (auto& f : x) {
        auto r = f.get().size();
        ::benchmark::DoNotOptimize(r);
      }
    }
  }

  template <size_t N>
  void
  getattr_bench(::benchmark::State& state, reader::getattr_options const& opts,
                std::array<std::string_view, N> const& paths) {
    int i = 0;
    std::vector<reader::inode_view> ent;
    ent.reserve(paths.size());
    for (auto const& p : paths) {
      ent.emplace_back(fs->find(p.data())->inode());
    }

    for (auto _ : state) {
      auto r = fs->getattr(ent[i++ % N], opts);
      ::benchmark::DoNotOptimize(r);
    }
  }

  template <size_t N>
  void getattr_bench(::benchmark::State& state,
                     std::array<std::string_view, N> const& paths) {
    getattr_bench(state, {}, paths);
  }

  std::unique_ptr<reader::filesystem_v2> fs;
  std::vector<reader::inode_view> inode_views;

 private:
  test::test_logger lgr;
  test::os_access_mock os;
  std::string image;
  std::shared_ptr<mmif> mm;
};

class filesystem_walk : public ::benchmark::Fixture {
 public:
  void SetUp(::benchmark::State const&) {
    mm = std::make_shared<test::mmap_mock>(get_image());
    reader::filesystem_options opts;
    opts.block_cache.max_bytes = 1 << 20;
    opts.metadata.enable_nlink = true;
    fs = std::make_unique<reader::filesystem_v2>(lgr, os, mm, opts);
    // fs->dump(std::cout, {.features = reader::fsinfo_features::for_level(2)});
  }

  void TearDown(::benchmark::State const&) {
    mm.reset();
    fs.reset();
  }

  std::unique_ptr<reader::filesystem_v2> fs;

 private:
  constexpr static int kDimension{32};
  constexpr static size_t kPatternLength{16};

  static std::string make_data(std::mt19937_64& rng, size_t size) {
    std::string data;
    std::uniform_int_distribution<> byte_dist{0, 31};
    data.reserve(size * kPatternLength * 2);
    for (size_t i = 0; i < size; ++i) {
      char p1 = byte_dist(rng);
      char p2 = 128 + byte_dist(rng);
      for (size_t j = 0; j < kPatternLength; ++j) {
        data.push_back(p1);
        data.push_back(p2);
      }
    }
    return data;
  }

  static void add_random_file_tree(test::os_access_mock& os) {
    std::mt19937_64 rng{42};
    std::uniform_int_distribution<> size_dist{1, 16};
    std::uniform_int_distribution<> path_comp_size_dist{1, 10};

    auto random_path_component = [&] {
      auto size = path_comp_size_dist(rng);
      return test::create_random_string(size, 'A', 'Z', rng);
    };

    for (int u = 0; u < kDimension; ++u) {
      std::filesystem::path d1{random_path_component() + std::to_string(u)};
      os.add_dir(d1);

      for (int v = 0; v < kDimension; ++v) {
        std::filesystem::path d2{d1 /
                                 (random_path_component() + std::to_string(v))};
        os.add_dir(d2);

        for (int w = 0; w < kDimension; ++w) {
          std::filesystem::path d3{
              d2 / (random_path_component() + std::to_string(w))};
          os.add_dir(d3);

          for (int z = 0; z < kDimension; ++z) {
            std::filesystem::path f{
                d3 / (random_path_component() + std::to_string(z))};
            os.add_file(f, make_data(rng, size_dist(rng)));
          }
        }
      }
    }
  }

  static std::string build_image() {
    auto os = std::make_shared<test::os_access_mock>();
    os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
    add_random_file_tree(*os);
    writer::segmenter_factory::config cfg;
    cfg.blockhash_window_size.set_default(4);
    cfg.window_increment_shift.set_default(1);
    cfg.max_active_blocks.set_default(4);
    cfg.bloom_filter_size.set_default(4);
    cfg.block_size_bits = 16;
    return make_filesystem(nullptr, os, &cfg);
  }

  static std::string get_image() {
    static std::string const image = [] {
      std::string image;
      if (auto file = std::getenv("DWARFS_BENCHMARK_SAVE_IMAGE")) {
        std::cerr << "*** Saving image to " << file << std::endl;
        image = build_image();
        std::ofstream ofs(file, std::ios::binary);
        ofs.write(image.data(), image.size());
      } else if (auto file = std::getenv("DWARFS_BENCHMARK_LOAD_IMAGE")) {
        std::cerr << "*** Loading image from " << file << std::endl;
        std::ifstream ifs(file, std::ios::binary);
        if (ifs) {
          ifs.seekg(0, std::ios::end);
          image.resize(ifs.tellg());
          ifs.seekg(0, std::ios::beg);
          ifs.read(image.data(), image.size());
        } else {
          throw std::runtime_error("Failed to open image file");
        }
      } else {
        image = build_image();
      }
      return image;
    }();
    return image;
  }

  test::test_logger lgr;
  test::os_access_mock os;
  std::shared_ptr<mmif> mm;
};

BENCHMARK_DEFINE_F(filesystem, find_path)(::benchmark::State& state) {
  std::array<char const*, 8> paths{{
      "/test.pl",
      "/somelink",
      "/baz.pl",
      "/ipsum.txt",
      "/somedir/ipsum.py",
      "/somedir/bad",
      "/somedir/null",
      "/somedir/zero",
  }};
  int i = 0;

  for (auto _ : state) {
    auto r = fs->find(paths[i++ % paths.size()]);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, find_inode)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->find(i++ % 8);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, find_inode_name)(::benchmark::State& state) {
  std::array<char const*, 4> names{{
      "ipsum.py",
      "bad",
      "null",
      "zero",
  }};
  auto base = fs->find("/somedir");
  auto inode_num = base->inode().inode_num();
  int i = 0;

  for (auto _ : state) {
    auto r = fs->find(inode_num, names[i++ % names.size()]);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, getattr_dir)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/", "/somedir"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_dir_nosize)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/", "/somedir"}};
  getattr_bench(state, {.no_size = true}, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file)(::benchmark::State& state) {
  std::array<std::string_view, 4> paths{
      {"/foo.pl", "/bar.pl", "/baz.pl", "/somedir/ipsum.py"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file_nosize)(::benchmark::State& state) {
  std::array<std::string_view, 4> paths{
      {"/foo.pl", "/bar.pl", "/baz.pl", "/somedir/ipsum.py"}};
  getattr_bench(state, {.no_size = true}, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file_large)(::benchmark::State& state) {
  std::array<std::string_view, 1> paths{{"/ipsum.txt"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file_large_nosize)
(::benchmark::State& state) {
  std::array<std::string_view, 1> paths{{"/ipsum.txt"}};
  getattr_bench(state, {.no_size = true}, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_link)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somelink", "/somedir/bad"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_link_nosize)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somelink", "/somedir/bad"}};
  getattr_bench(state, {.no_size = true}, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_dev)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somedir/null", "/somedir/zero"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_dev_nosize)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somedir/null", "/somedir/zero"}};
  getattr_bench(state, {.no_size = true}, paths);
}

BENCHMARK_DEFINE_F(filesystem, access_F_OK)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->access(inode_views[i++ % NUM_ENTRIES], F_OK, 1000, 100);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, access_R_OK)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->access(inode_views[i++ % NUM_ENTRIES], R_OK, 1000, 100);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, opendir)(::benchmark::State& state) {
  auto dev = fs->find("/somedir");
  auto iv = dev->inode();

  for (auto _ : state) {
    auto r = fs->opendir(iv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, dirsize)(::benchmark::State& state) {
  auto dev = fs->find("/somedir");
  auto dv = fs->opendir(dev->inode());

  for (auto _ : state) {
    auto r = fs->dirsize(*dv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, readdir)(::benchmark::State& state) {
  auto dev = fs->find("/");
  auto dv = fs->opendir(dev->inode());
  auto const num = fs->dirsize(*dv);
  size_t i = 0;

  for (auto _ : state) {
    auto r = fs->readdir(*dv, i % num);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, readlink)(::benchmark::State& state) {
  auto dev = fs->find("/somelink");
  auto iv = dev->inode();

  for (auto _ : state) {
    auto r = fs->readlink(iv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, statvfs)(::benchmark::State& state) {
  for (auto _ : state) {
    vfs_stat buf;
    fs->statvfs(&buf);
    ::benchmark::DoNotOptimize(buf);
  }
}

BENCHMARK_DEFINE_F(filesystem, open)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    std::error_code ec;
    auto r = fs->open(inode_views[i++ % NUM_ENTRIES], ec);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, read_small)(::benchmark::State& state) {
  read_bench(state, "/somedir/ipsum.py");
}

BENCHMARK_DEFINE_F(filesystem, read_large)(::benchmark::State& state) {
  read_bench(state, "/ipsum.txt");
}

BENCHMARK_DEFINE_F(filesystem, read_string_small)(::benchmark::State& state) {
  read_string_bench(state, "/somedir/ipsum.py");
}

BENCHMARK_DEFINE_F(filesystem, read_string_large)(::benchmark::State& state) {
  read_string_bench(state, "/ipsum.txt");
}

BENCHMARK_DEFINE_F(filesystem, readv_small)(::benchmark::State& state) {
  readv_bench(state, "/somedir/ipsum.py");
}

BENCHMARK_DEFINE_F(filesystem, readv_large)(::benchmark::State& state) {
  readv_bench(state, "/ipsum.txt");
}

BENCHMARK_DEFINE_F(filesystem, readv_future_small)(::benchmark::State& state) {
  readv_future_bench(state, "/somedir/ipsum.py");
}

BENCHMARK_DEFINE_F(filesystem, readv_future_large)(::benchmark::State& state) {
  readv_future_bench(state, "/ipsum.txt");
}

BENCHMARK_DEFINE_F(filesystem_walk, walk)(::benchmark::State& state) {
  for (auto _ : state) {
    fs->walk([](reader::dir_entry_view) {});
  }
}

BENCHMARK_DEFINE_F(filesystem_walk, walk_data_order)
(::benchmark::State& state) {
  for (auto _ : state) {
    fs->walk_data_order([](reader::dir_entry_view) {});
  }
}

} // namespace

BENCHMARK(frozen_legacy_string_table_lookup);

BENCHMARK(frozen_string_table_lookup)
    ->Args({false, false})
    ->Args({false, true})
    ->Args({true, false})
    ->Args({true, true});

BENCHMARK(dwarfs_initialize)->Apply(PackParams)->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(filesystem, find_inode)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, find_inode_name)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, find_path)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, getattr_dir)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_dir_nosize)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_link)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_link_nosize)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file_nosize)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file_large_nosize)
    ->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_dev)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_dev_nosize)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, access_F_OK)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, access_R_OK)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, opendir)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, dirsize)->Apply(PackParamsDirs);
BENCHMARK_REGISTER_F(filesystem, readdir)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, readlink)->Apply(PackParamsStrings);
BENCHMARK_REGISTER_F(filesystem, statvfs)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, open)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, read_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, read_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, read_string_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, read_string_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_future_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_future_large)->Apply(PackParamsNone);

BENCHMARK_REGISTER_F(filesystem_walk, walk)->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(filesystem_walk, walk_data_order)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
