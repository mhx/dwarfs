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
#include <iostream>
#include <string>

#ifndef _WIN32
#include <sys/statvfs.h>
#else
#include <pro-statvfs.h>
#endif
#include <boost/program_options.hpp>

#include <folly/String.h>

#include "dwarfs/filesystem_extractor.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/util.h"
#include "dwarfs/version.h"

namespace po = boost::program_options;

using namespace dwarfs;

namespace {

int dwarfsextract(int argc, char** argv) {
  std::string filesystem, output, format, cache_size_str, log_level,
      image_offset;
  size_t num_workers;

  // clang-format off
  po::options_description opts("Command line options");
  opts.add_options()
    ("input,i",
        po::value<std::string>(&filesystem),
        "input filesystem file")
    ("output,o",
        po::value<std::string>(&output),
        "output file or directory")
    ("image-offset,O",
        po::value<std::string>(&image_offset)->default_value("auto"),
        "filesystem image offset in bytes")
    ("format,f",
        po::value<std::string>(&format),
        "output format")
    ("num-workers,n",
        po::value<size_t>(&num_workers)->default_value(4),
        "number of worker threads")
    ("cache-size,s",
        po::value<std::string>(&cache_size_str)->default_value("512m"),
        "block cache size")
    ("log-level,l",
        po::value<std::string>(&log_level)->default_value("warn"),
        "log level (error, warn, info, debug, trace)")
    ("help,h",
        "output help message and exit");
  // clang-format on

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, opts), vm);
    po::notify(vm);
  } catch (po::error const& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }

  if (vm.count("help") or !vm.count("input")) {
    std::cerr << "dwarfsextract (" << PRJ_GIT_ID << ")\n\n"
              << opts << std::endl;
    return 0;
  }

  try {
    auto level = logger::parse_level(log_level);
    stream_logger lgr(std::cerr, level, level >= logger::DEBUG);
    filesystem_options fsopts;
    try {
      fsopts.image_offset = image_offset == "auto"
                                ? filesystem_options::IMAGE_OFFSET_AUTO
                                : folly::to<off_t>(image_offset);
    } catch (...) {
      DWARFS_THROW(runtime_error, "failed to parse offset: " + image_offset);
    }

    fsopts.block_cache.max_bytes = parse_size_with_unit(cache_size_str);
    fsopts.block_cache.num_workers = num_workers;
    fsopts.metadata.enable_nlink = true;

    filesystem_v2 fs(lgr, std::make_shared<mmap>(filesystem), fsopts);
    filesystem_extractor fsx(lgr);

    size_t max_queued_bytes = 0;

    {
      struct ::statvfs buf;
      fs.statvfs(&buf);
      if (fsopts.block_cache.max_bytes > buf.f_bsize) {
        max_queued_bytes = fsopts.block_cache.max_bytes - buf.f_bsize;
      }
    }

    if (format.empty()) {
      fsx.open_disk(output);
    } else {
      if (output == "-") {
        output.clear();
      }
      fsx.open_archive(output, format);
    }

    fsx.extract(fs, max_queued_bytes);

    fsx.close();
  } catch (runtime_error const& e) {
    std::cerr << folly::exceptionStr(e) << std::endl;
    return 1;
  } catch (system_error const& e) {
    std::cerr << folly::exceptionStr(e) << std::endl;
    return 1;
  } catch (std::system_error const& e) {
    std::cerr << folly::exceptionStr(e) << std::endl;
    return 1;
  }

  return 0;
}

} // namespace

int main(int argc, char** argv) {
  return dwarfs::safe_main([&] { return dwarfsextract(argc, argv); });
}
