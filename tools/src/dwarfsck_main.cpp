/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/checksum.h>
#include <dwarfs/config.h>
#include <dwarfs/conv.h>
#include <dwarfs/counting_semaphore.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/string.h>
#include <dwarfs/superblock_editor.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/program_options_helpers.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs/util.h>
#include <dwarfs_tool_main.h>
#include <dwarfs_tool_manpage.h>

namespace dwarfs::tool {

namespace po = boost::program_options;

namespace {

class dwarfsck_impl {
 public:
  struct options {
    sys_string input;
    std::optional<sys_string> export_metadata;
    std::optional<std::string> checksum_algo;
    std::optional<sys_string> set_label;
    std::string cache_size_str;
    std::string image_offset;
    std::string detail;
#if DWARFS_PERFMON_ENABLED
    std::unordered_set<std::string> perfmon_enabled;
    std::optional<std::filesystem::path> perfmon_trace_file;
#endif
    logger_options logopts;
    size_t num_workers{0};
    bool quiet{false};
    bool verbose{false};
    bool output_json{false};
    bool check_integrity{false};
    bool no_check{false};
    bool print_header{false};
    bool list_files{false};
    bool init_superblock{false};
  };

  // Parses the command line. On success, returns the parsed options.
  // On failure, or if the command line has already been fully handled
  // (`--help`, `--man`), returns the exit code for `main`.
  static std::expected<options, int>
  parse_cmdline(int argc, sys_char** argv, iolayer const& iol);

  dwarfsck_impl(iolayer const& iol, options const& opts)
      : iol_{iol}
      , opts_{opts}
      , lgr_{iol.term, iol.err, *iol.os, opts.logopts}
      , LOG_PROXY_INIT(lgr_) {}

  int run();

 private:
  bool validate_options() const;

  int do_print_header(file_view const& mm);

  int do_export_metadata();
  int do_check();
  void do_dump_info();
  void do_list_files();
  void do_checksum();
  void do_edit_superblock(std::filesystem::path const& image_path,
                          std::uint64_t fs_offset, std::uint64_t fs_size);

  reader::filesystem_v2& fs() { return fs_.value(); }

  iolayer const& iol_;
  options const opts_;
  stream_logger lgr_;
  LOG_PROXY_DECL(debug_logger_policy);
  reader::filesystem_options fsopts_;
  std::optional<reader::filesystem_v2> fs_;
};

std::expected<dwarfsck_impl::options, int>
dwarfsck_impl::parse_cmdline(int argc, sys_char** argv, iolayer const& iol) {
  size_t const num_cpu = std::max(hardware_concurrency(), 1U);

  auto algo_list = checksum::available_algorithms();
  auto checksum_desc = fmt::format("print checksums for all files ({})",
                                   fmt::join(algo_list, ", "));
  auto detail_desc = fmt::format(
      "detail level (0-{}, or feature list: {})",
      reader::fsinfo_features::max_level(),
      fmt::join(reader::fsinfo_features::all().to_string_views(), ", "));
  auto const detail_default{reader::fsinfo_features::for_level(2).to_string()};

  options o;

  // program_options cannot bind to std::optional<>, so these are parsed into
  // raw values and moved into the optionals below if the option was present.
  sys_string export_metadata_raw;
  sys_string set_label_raw;
  std::string checksum_algo_raw;
#if DWARFS_PERFMON_ENABLED
  std::string perfmon_enabled_raw;
  sys_string perfmon_trace_file_raw;
#endif

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po_sys_value<sys_string>(&o.input),
        "input filesystem")
    ("detail,d",
        po::value<std::string>(&o.detail)->default_value(detail_default),
        detail_desc.c_str())
    ("quiet,q",
        po::value<bool>(&o.quiet)->zero_tokens(),
        "don't print anything unless an error occurs")
    ("verbose,v",
        po::value<bool>(&o.verbose)->zero_tokens(),
        "produce verbose output")
    ("image-offset,O",
        po::value<std::string>(&o.image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
    ("print-header,H",
        po::value<bool>(&o.print_header)->zero_tokens(),
        "print filesystem header to stdout and exit")
    ("list,l",
        po::value<bool>(&o.list_files)->zero_tokens(),
        "list all files and exit")
    ("checksum",
        po::value<std::string>(&checksum_algo_raw),
        checksum_desc.c_str())
    ("num-workers,n",
        po::value<size_t>(&o.num_workers)->default_value(num_cpu),
        "number of reader worker threads")
    ("cache-size,s",
        po::value<std::string>(&o.cache_size_str)->default_value("512m"),
        "block cache size")
    ("check-integrity",
        po::value<bool>(&o.check_integrity)->zero_tokens(),
        "check integrity of each block")
    ("no-check",
        po::value<bool>(&o.no_check)->zero_tokens(),
        "don't even verify block checksums")
    ("json,j",
        po::value<bool>(&o.output_json)->zero_tokens(),
        "print information in JSON format")
    ("export-metadata",
        po_sys_value<sys_string>(&export_metadata_raw),
        "export raw metadata as JSON to file")
    ("init-superblock",
        po::value<bool>(&o.init_superblock)->zero_tokens(),
        "initialize filesystem superblock")
    ("set-label",
        po_sys_value<sys_string>(&set_label_raw),
        "set filesystem label")
#if DWARFS_PERFMON_ENABLED
    ("perfmon",
        po::value<std::string>(&perfmon_enabled_raw),
        "enable performance monitor")
    ("perfmon-trace",
        po_sys_value<sys_string>(&perfmon_trace_file_raw),
        "write performance monitor trace file")
#endif
    ;
  // clang-format on

  tool::add_common_options(opts, o.logopts);

  po::positional_options_description pos;
  pos.add("input", -1);

  po::variables_map vm;

  try {
    po::store(po::basic_command_line_parser<sys_char>(argc, argv)
                  .options(opts)
                  .positional(pos)
                  .run(),
              vm);
    po::notify(vm);
  } catch (po::error const& e) {
    iol.err << "error: " << e.what() << "\n";
    return std::unexpected(1);
  }

#ifdef DWARFS_BUILTIN_MANPAGE
  if (vm.contains("man")) {
    tool::show_manpage(tool::manpage::get_dwarfsck_manpage(), iol);
    return std::unexpected(0);
  }
#endif

#if DWARFS_PERFMON_ENABLED
  if (vm.contains("perfmon")) {
    split_to(perfmon_enabled_raw, ',', o.perfmon_enabled);
  }

  if (vm.contains("perfmon-trace")) {
    o.perfmon_trace_file = iol.os->canonical(perfmon_trace_file_raw);
  }
#endif

  constexpr auto usage = "Usage: dwarfsck [OPTIONS...]\n";

  if (vm.contains("help") or !vm.contains("input")) {
    auto extra_deps = [](library_dependencies& deps) {
      decompressor_registry::instance().add_library_dependencies(deps);
    };
    iol.out << tool::tool_header("dwarfsck", extra_deps) << usage << "\n"
            << opts << "\n";
    return std::unexpected(0);
  }

  if (vm.contains("checksum")) {
    o.checksum_algo = std::move(checksum_algo_raw);
  }

  if (vm.contains("export-metadata")) {
    o.export_metadata = std::move(export_metadata_raw);
  }

  if (vm.contains("set-label")) {
    o.set_label = std::move(set_label_raw);
  }

  return o;
}

bool dwarfsck_impl::validate_options() const {
  if (opts_.no_check && opts_.check_integrity) {
    LOG_WARN << "--no-check and --check-integrity are mutually exclusive";
    return false;
  }

  if (opts_.checksum_algo && !checksum::is_available(*opts_.checksum_algo)) {
    LOG_WARN << "checksum algorithm not available: " << *opts_.checksum_algo;
    return false;
  }

  if (opts_.num_workers < 1) {
    LOG_WARN << "number of worker threads must be at least 1";
    return false;
  }

  if (opts_.print_header &&
      (opts_.output_json || opts_.export_metadata || opts_.check_integrity ||
       opts_.list_files || opts_.checksum_algo)) {
    LOG_WARN << "--print-header is mutually exclusive with --json, "
                "--export-metadata, --check-integrity, --list and --checksum";
    return false;
  }

  return true;
}

int dwarfsck_impl::run() {
  if (!validate_options()) {
    return 1;
  }

  try {
    fsopts_.metadata.check_consistency = !opts_.no_check;
    fsopts_.image_offset = reader::parse_image_offset(opts_.image_offset);
    fsopts_.block_cache.max_bytes = parse_size_with_unit(opts_.cache_size_str);
    fsopts_.block_cache.num_workers = opts_.num_workers;

    auto input_path = iol_.os->canonical(opts_.input);
    auto mm = iol_.os->open_file(input_path);
    std::shared_ptr<performance_monitor> perfmon;

#if DWARFS_PERFMON_ENABLED
    perfmon = performance_monitor::create(opts_.perfmon_enabled, iol_.file,
                                          opts_.perfmon_trace_file);
#endif

    if (opts_.print_header) {
      return do_print_header(mm);
    }

    fs_.emplace(lgr_, *iol_.os, mm, fsopts_, perfmon);

    if (opts_.export_metadata) {
      return do_export_metadata();
    }

    auto const errors = do_check();

    if (!opts_.quiet && !opts_.list_files && !opts_.checksum_algo &&
        !opts_.init_superblock && !opts_.set_label) {
      do_dump_info();
    }

    if (opts_.list_files) {
      do_list_files();
    }

    if (opts_.checksum_algo) {
      do_checksum();
    }

#if DWARFS_PERFMON_ENABLED
    if (perfmon) {
      perfmon->summarize(iol_.err);
    }
#endif

    if (errors > 0) {
      return 1;
    }

    if (opts_.init_superblock || opts_.set_label) {
      auto const fs_offset = fs_->image_offset();
      auto const fs_size = fs_->image_size();
      fs_.reset();

      do_edit_superblock(input_path, fs_offset, fs_size);
    }
  } catch (std::exception const& e) {
    LOG_ERROR << "error: " << e.what();
    return 1;
  }

  return 0;
}

int dwarfsck_impl::do_print_header(file_view const& mm) {
  auto hdr = reader::filesystem_v2::header(lgr_, mm, fsopts_.image_offset);

  if (!hdr) {
    LOG_WARN << "filesystem does not contain a header";
    return 2;
  }

  ensure_binary_mode(iol_.out);

  for (auto const& ext : *hdr) {
    for (auto const& seg : ext.segments()) {
      auto const data = seg.span<char>();
      iol_.out.write(data.data(), data.size());
    }
  }

  if (iol_.out.bad() || iol_.out.fail()) {
    LOG_ERROR << "error writing header";
    return 1;
  }

  return 0;
}

int dwarfsck_impl::do_export_metadata() {
  auto const export_path = std::filesystem::path(*opts_.export_metadata);
  std::error_code ec;
  std::unique_ptr<output_stream> of;

  if (export_path != "-") {
    of = iol_.file->open_output(iol_.os->canonical(export_path), ec);
    if (ec) {
      LOG_ERROR << "failed to open metadata output file: " << ec.message();
      return 1;
    }
  }

  fs().serialize_metadata_as_json(of ? of->os() : iol_.out, false);

  if (of) {
    of->close(ec);
    if (ec) {
      LOG_ERROR << "failed to close metadata output file: " << ec.message();
      return 1;
    }
  }

  return 0;
}

int dwarfsck_impl::do_check() {
  auto const level = opts_.check_integrity
                         ? reader::filesystem_check_level::FULL
                         : reader::filesystem_check_level::CHECKSUM;
  auto errors = opts_.no_check ? 0 : fs().check(level, opts_.num_workers);

  if (!opts_.no_check && opts_.check_integrity) {
    try {
      fs().walk([](auto const&) {});
    } catch (std::exception const& e) {
      LOG_ERROR << "error: failed to walk filesystem: " << exception_str(e);
      ++errors;
    }
  }

  return errors;
}

void dwarfsck_impl::do_dump_info() {
  reader::fsinfo_options fsi_opts;

  fsi_opts.block_access = opts_.no_check
                              ? reader::block_access_level::no_verify
                              : reader::block_access_level::unrestricted;

  auto numeric_detail = try_to<int>(opts_.detail);
  fsi_opts.features = numeric_detail.has_value()
                          ? reader::fsinfo_features::for_level(*numeric_detail)
                          : reader::fsinfo_features::parse(opts_.detail);

  if (opts_.output_json) {
    iol_.out << fs().info_as_json(fsi_opts) << "\n";
  } else {
    fs().dump(iol_.out, fsi_opts);
  }
}

void dwarfsck_impl::do_list_files() {
  auto max_width = [](auto const& vec, std::string_view what) {
    DWARFS_CHECK(!vec.empty(), fmt::format("no {} in filesystem", what));
    auto max = std::ranges::max_element(vec);
    return std::to_string(*max).size();
  };

  auto const uid_width = max_width(fs().get_all_uids(), "uids");
  auto const gid_width = max_width(fs().get_all_gids(), "gids");

  size_t inode_size_width{0};

  if (opts_.verbose) {
    file_stat::off_type max_inode_size{0};
    fs().walk([&](auto const& de) {
      auto st = fs().getattr(de.inode());
      max_inode_size = std::max(max_inode_size, st.size());
    });
    inode_size_width = fmt::format("{:L}", max_inode_size).size();
  }

  fs().walk([&](auto const& de) {
    auto name = de.unix_path();

    if (opts_.verbose) {
      auto iv = de.inode();

      if (iv.is_symlink()) {
        name += " -> " + fs().readlink(iv);
      }

      auto st = fs().getattr(iv);

      fmt::print(iol_.out, "{3} {4:{0}}/{5:{1}} {6:{2}L} {7:%F %H:%M} {8}\n",
                 uid_width, gid_width, inode_size_width, iv.mode_string(),
                 iv.getuid(), iv.getgid(), st.size(),
                 safe_localtime(st.mtime()), name);
    } else if (!name.empty()) {
      fmt::print(iol_.out, "{}\n", name);
    }
  });
}

void dwarfsck_impl::do_checksum() {
  auto const& algo = *opts_.checksum_algo;
  auto const max_queued_bytes = fsopts_.block_cache.max_bytes;

  struct cache_entry {
    explicit cache_entry(reader::duplication_info const& dup_info)
        : remaining{dup_info.duplication_count} {}

    std::size_t remaining;
    std::optional<std::string> checksum;
    std::vector<std::string> paths;
  };

  std::mutex mx;
  std::unordered_map<uint32_t, cache_entry> checksum_cache;
  counting_semaphore sem;
  sem.post(static_cast<int64_t>(max_queued_bytes));

  thread_pool pool{lgr_, *iol_.os, "checksum", opts_.num_workers};

  size_t const max_queued_per_worker = max_queued_bytes / opts_.num_workers;

  auto build_hexdigest = [&algo](auto const& ranges) {
    thread_local checksum cs(algo);

    cs.reset();

    for (auto const& r : ranges) {
      cs.update(r.data(), r.size());
    }

    return cs.hexdigest();
  };

  auto print_cs = [&](std::string const& hexdigest, std::string const& path) {
    fmt::print(iol_.out, "{}  {}\n", hexdigest, path);
  };

  for (auto const& de : fs().entries_in_data_order()) {
    auto iv = de.inode();

    if (iv.is_regular_file()) {
      auto const dup_info = fs().get_duplication_info(iv);

      if (dup_info.duplication_count > 1) {
        std::lock_guard lock(mx);

        auto it = checksum_cache.find(dup_info.unique_content_id);

        if (it != checksum_cache.end()) {
          if (it->second.checksum) {
            print_cs(*it->second.checksum, de.unix_path());
            assert(it->second.paths.empty());
            if (--it->second.remaining == 0) {
              checksum_cache.erase(it);
            }
          } else {
            it->second.paths.push_back(de.unix_path());
          }
          continue;
        }

        auto const r [[maybe_unused]] = checksum_cache.emplace(
            dup_info.unique_content_id, cache_entry{dup_info});
        assert(r.second);
      }

      reader::detail::file_reader fr(fs(), iv);

      pool.add_job(
          [this, &build_hexdigest, &checksum_cache, &mx, &print_cs, de,
           dup_info,
           ranges = fr.read_sequential(sem, max_queued_per_worker)]() mutable {
            try {
              auto const path = de.unix_path();
              auto hexdigest = build_hexdigest(ranges);

              {
                std::lock_guard lock(mx);
                print_cs(hexdigest, path);

                if (dup_info.duplication_count > 1) {
                  auto it = checksum_cache.find(dup_info.unique_content_id);

                  assert(it != checksum_cache.end());

                  for (auto const& p : it->second.paths) {
                    print_cs(hexdigest, p);
                  }

                  assert(std::cmp_greater(it->second.remaining,
                                          it->second.paths.size()));

                  it->second.remaining -= it->second.paths.size();
                  it->second.paths.clear();

                  if (--it->second.remaining == 0) {
                    checksum_cache.erase(it);
                  } else {
                    it->second.checksum = std::move(hexdigest);
                  }
                }
              }
            } catch (std::exception const& e) {
              LOG_ERROR << "error processing inode for " << de.unix_path()
                        << ": " << e.what();
            }
          });
    }
  }

  pool.wait();

#ifdef NDEBUG
  {
    std::lock_guard lock(mx);
    assert(checksum_cache.empty());
  }
#endif
}

void dwarfsck_impl::do_edit_superblock(std::filesystem::path const& image_path,
                                       std::uint64_t const fs_offset,
                                       std::uint64_t const fs_size) {
  auto io_stream = iol_.file->open(image_path, std::ios::in | std::ios::out |
                                                   std::ios::binary);
  auto& ios = io_stream->ios();
  auto sbe = superblock_editor{};

  ios.seekg(fs_offset);
  sbe.read(ios);

  if (opts_.init_superblock) {
    if (!sbe.fs_size()) {
      sbe.init_fs_size(fs_size);
    }

    if (!sbe.fs_uuid()) {
      sbe.init_fs_uuid();
    }
  }

  if (opts_.set_label) {
    sbe.set_fs_label(sys_string_to_string(*opts_.set_label));
  }

  sbe.update(ios);

  io_stream->close();
}

} // namespace

int dwarfsck_main(int argc, sys_char** argv, iolayer const& iol) {
  auto opts = dwarfsck_impl::parse_cmdline(argc, argv, iol);

  if (!opts) {
    return opts.error();
  }

  return dwarfsck_impl{iol, *opts}.run();
}

} // namespace dwarfs::tool
