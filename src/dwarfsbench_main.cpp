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

#include <exception>

#include <boost/program_options.hpp>

#include <folly/Conv.h>
#include <folly/String.h>

#include "dwarfs/file_stat.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"
#include "dwarfs_tool_main.h"

namespace po = boost::program_options;

namespace dwarfs {

int dwarfsbench_main(int argc, sys_char** argv) {
  std::string filesystem, cache_size_str, lock_mode_str, decompress_ratio_str,
      log_level;
  size_t num_workers;
  size_t num_readers;

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("filesystem,f",
        po::value<std::string>(&filesystem),
        "path to filesystem")
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(1),
        "number of worker threads")
    ("num-readers,N",
        po::value<size_t>(&num_readers)->default_value(1),
        "number of reader threads")
    ("cache-size,s",
        po::value<std::string>(&cache_size_str)->default_value("256m"),
        "block cache size")
    ("lock-mode,m",
        po::value<std::string>(&lock_mode_str)->default_value("none"),
        "mlock mode (none, try, must)")
    ("decompress-ratio,r",
        po::value<std::string>(&decompress_ratio_str)->default_value("0.8"),
        "block cache size")
    ("log-level,l",
        po::value<std::string>(&log_level)->default_value("info"),
        "log level (error, warn, info, debug, trace)")
    ("help,h",
        "output help message and exit");
  // clang-format on

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, opts), vm);
    po::notify(vm);
  } catch (po::error const& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  if (vm.count("help") or !vm.count("filesystem")) {
    std::cout << tool_header("dwarfsbench") << opts << "\n";
    return 0;
  }

  try {
    stream_logger lgr(std::cerr, logger::parse_level(log_level));
    filesystem_options fsopts;

    fsopts.lock_mode = parse_mlock_mode(lock_mode_str);
    fsopts.block_cache.max_bytes = parse_size_with_unit(cache_size_str);
    fsopts.block_cache.num_workers = num_workers;
    fsopts.block_cache.decompress_ratio =
        folly::to<double>(decompress_ratio_str);

    dwarfs::filesystem_v2 fs(lgr, std::make_shared<dwarfs::mmap>(filesystem),
                             fsopts);

    worker_group wg("reader", num_readers);

    fs.walk([&](auto entry) {
      auto inode_data = entry.inode();
      if (inode_data.is_regular_file()) {
        wg.add_job([&fs, inode_data] {
          try {
            file_stat stbuf;
            if (fs.getattr(inode_data, &stbuf) == 0) {
              std::vector<char> buf(stbuf.size);
              int fh = fs.open(inode_data);
              fs.read(fh, buf.data(), buf.size());
            }
          } catch (runtime_error const& e) {
            std::cerr << "error: " << e.what() << "\n";
          } catch (...) {
            std::cerr << "error: "
                      << folly::exceptionStr(std::current_exception()) << "\n";
            dump_exceptions();
          }
        });
      }
    });

    wg.wait();
  } catch (runtime_error const& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

} // namespace dwarfs
