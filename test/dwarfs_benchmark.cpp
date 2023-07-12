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

#include <sstream>

#include <benchmark/benchmark.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/entry.h"
#include "dwarfs/file_stat.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/iovec_read_buf.h"
#include "dwarfs/logger.h"
#include "dwarfs/options.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/string_table.h"
#include "dwarfs/vfs_stat.h"
#include "dwarfs/worker_group.h"
#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_strings.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"

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

std::string make_filesystem(::benchmark::State const& state) {
  block_manager::config cfg;
  scanner_options options;

  cfg.blockhash_window_size = 8;
  cfg.block_size_bits = 12;

  options.with_devices = true;
  options.with_specials = true;
  options.inode.with_similarity = false;
  options.inode.with_nilsimsa = false;
  options.keep_all_times = false;
  options.pack_chunk_table = true;
  options.pack_directories = state.range(0);
  options.pack_shared_files_table = true;
  options.pack_names = state.range(2);
  options.pack_names_index = state.range(3);
  options.pack_symlinks = state.range(2);
  options.pack_symlinks_index = state.range(3);
  options.force_pack_string_tables = true;
  options.plain_names_table = state.range(1);
  options.plain_symlinks_table = state.range(1);

  worker_group wg("writer", 4);

  std::ostringstream logss;
  stream_logger lgr(logss); // TODO: mock
  lgr.set_policy<prod_logger_policy>();

  scanner s(lgr, wg, cfg, entry_factory::create(),
            test::os_access_mock::create_test_instance(),
            std::make_shared<test::script_mock>(), options);

  std::ostringstream oss;
  progress prog([](const progress&, bool) {}, 1000);

  block_compressor bc("null");
  filesystem_writer fsw(oss, lgr, wg, prog, bc);

  s.scan(fsw, "", prog);

  return oss.str();
}

template <typename T>
auto make_frozen_string_table(T const& strings,
                              string_table::pack_options const& options) {
  using namespace apache::thrift::frozen;
  std::string tmp;
  auto tbl = string_table::pack(strings, options);
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
  string_table table(data);
  int i = 0;
  std::string str;

  for (auto _ : state) {
    ::benchmark::DoNotOptimize(str = table[i++ % test::NUM_STRINGS]);
  }
}

void frozen_string_table_lookup(::benchmark::State& state) {
  auto data = make_frozen_string_table(
      test::test_strings,
      string_table::pack_options(state.range(0), state.range(1), true));
  stream_logger lgr;
  string_table table(lgr, "bench", data);
  int i = 0;
  std::string str;

  for (auto _ : state) {
    ::benchmark::DoNotOptimize(str = table[i++ % test::NUM_STRINGS]);
  }
}

void dwarfs_initialize(::benchmark::State& state) {
  auto image = make_filesystem(state);
  stream_logger lgr;
  auto mm = std::make_shared<test::mmap_mock>(image);
  filesystem_options opts;
  opts.block_cache.max_bytes = 1 << 20;
  opts.metadata.enable_nlink = true;

  for (auto _ : state) {
    auto fs = filesystem_v2(lgr, mm, opts);
    ::benchmark::DoNotOptimize(fs);
  }
}

class filesystem : public ::benchmark::Fixture {
 public:
  static constexpr size_t NUM_ENTRIES = 8;

  void SetUp(::benchmark::State const& state) {
    image = make_filesystem(state);
    mm = std::make_shared<test::mmap_mock>(image);
    filesystem_options opts;
    opts.block_cache.max_bytes = 1 << 20;
    opts.metadata.enable_nlink = true;
    fs = std::make_unique<filesystem_v2>(lgr, mm, opts);
    entries.reserve(NUM_ENTRIES);
    for (int i = 0; entries.size() < NUM_ENTRIES; ++i) {
      if (auto e = fs->find(i)) {
        entries.emplace_back(*e);
      }
    }
  }

  void TearDown(::benchmark::State const&) {
    entries.clear();
    image.clear();
    mm.reset();
    fs.reset();
  }

  void read_bench(::benchmark::State& state, const char* file) {
    auto iv = fs->find(file);
    file_stat st;
    fs->getattr(*iv, &st);
    auto i = fs->open(*iv);
    std::string buf;
    buf.resize(st.size);

    for (auto _ : state) {
      auto r = fs->read(i, buf.data(), st.size);
      ::benchmark::DoNotOptimize(r);
    }
  }

  void readv_bench(::benchmark::State& state, char const* file) {
    auto iv = fs->find(file);
    file_stat st;
    fs->getattr(*iv, &st);
    auto i = fs->open(*iv);

    for (auto _ : state) {
      iovec_read_buf buf;
      auto r = fs->readv(i, buf, st.size);
      ::benchmark::DoNotOptimize(r);
    }
  }

  void readv_future_bench(::benchmark::State& state, char const* file) {
    auto iv = fs->find(file);
    file_stat st;
    fs->getattr(*iv, &st);
    auto i = fs->open(*iv);

    for (auto _ : state) {
      auto x = fs->readv(i, st.size);
      for (auto& f : *x) {
        auto r = f.get().size();
        ::benchmark::DoNotOptimize(r);
      }
    }
  }

  template <size_t N>
  void getattr_bench(::benchmark::State& state,
                     std::array<std::string_view, N> const& paths) {
    int i = 0;
    std::vector<inode_view> ent;
    ent.reserve(paths.size());
    for (auto const& p : paths) {
      ent.emplace_back(*fs->find(p.data()));
    }

    for (auto _ : state) {
      file_stat buf;
      auto r = fs->getattr(ent[i++ % N], &buf);
      ::benchmark::DoNotOptimize(r);
    }
  }

  std::unique_ptr<filesystem_v2> fs;
  std::vector<inode_view> entries;

 private:
  stream_logger lgr;
  std::string image;
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
  int i = 0;

  for (auto _ : state) {
    auto r = fs->find(base->inode_num(), names[i++ % names.size()]);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, getattr_dir)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/", "/somedir"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file)(::benchmark::State& state) {
  std::array<std::string_view, 4> paths{
      {"/foo.pl", "/bar.pl", "/baz.pl", "/somedir/ipsum.py"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_file_large)(::benchmark::State& state) {
  std::array<std::string_view, 1> paths{{"/ipsum.txt"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_link)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somelink", "/somedir/bad"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, getattr_dev)(::benchmark::State& state) {
  std::array<std::string_view, 2> paths{{"/somedir/null", "/somedir/zero"}};
  getattr_bench(state, paths);
}

BENCHMARK_DEFINE_F(filesystem, access_F_OK)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->access(entries[i++ % NUM_ENTRIES], F_OK, 1000, 100);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, access_R_OK)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->access(entries[i++ % NUM_ENTRIES], R_OK, 1000, 100);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, opendir)(::benchmark::State& state) {
  auto iv = fs->find("/somedir");

  for (auto _ : state) {
    auto r = fs->opendir(*iv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, dirsize)(::benchmark::State& state) {
  auto iv = fs->find("/somedir");
  auto dv = fs->opendir(*iv);

  for (auto _ : state) {
    auto r = fs->dirsize(*dv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, readdir)(::benchmark::State& state) {
  auto iv = fs->find("/");
  auto dv = fs->opendir(*iv);
  auto const num = fs->dirsize(*dv);
  size_t i = 0;

  for (auto _ : state) {
    auto r = fs->readdir(*dv, i % num);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, readlink)(::benchmark::State& state) {
  auto iv = fs->find("/somelink");

  for (auto _ : state) {
    auto r = fs->readlink(*iv);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, statvfs)(::benchmark::State& state) {
  for (auto _ : state) {
    vfs_stat buf;
    auto r = fs->statvfs(&buf);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, open)(::benchmark::State& state) {
  int i = 0;

  for (auto _ : state) {
    auto r = fs->open(entries[i++ % NUM_ENTRIES]);
    ::benchmark::DoNotOptimize(r);
  }
}

BENCHMARK_DEFINE_F(filesystem, read_small)(::benchmark::State& state) {
  read_bench(state, "/somedir/ipsum.py");
}

BENCHMARK_DEFINE_F(filesystem, read_large)(::benchmark::State& state) {
  read_bench(state, "/ipsum.txt");
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

} // namespace

BENCHMARK(frozen_legacy_string_table_lookup);

BENCHMARK(frozen_string_table_lookup)
    ->Args({false, false})
    ->Args({false, true})
    ->Args({true, false})
    ->Args({true, true});

BENCHMARK(dwarfs_initialize)->Apply(PackParams);

BENCHMARK_REGISTER_F(filesystem, find_inode)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, find_inode_name)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, find_path)->Apply(PackParams);
BENCHMARK_REGISTER_F(filesystem, getattr_dir)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_link)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_file_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, getattr_dev)->Apply(PackParamsNone);
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
BENCHMARK_REGISTER_F(filesystem, readv_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_large)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_future_small)->Apply(PackParamsNone);
BENCHMARK_REGISTER_F(filesystem, readv_future_large)->Apply(PackParamsNone);

BENCHMARK_MAIN();
