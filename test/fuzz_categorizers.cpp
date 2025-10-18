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

#include <boost/program_options.hpp>

#include <dwarfs/file_access.h>
#include <dwarfs/file_access_generic.h>
#include <dwarfs/file_view.h>
#include <dwarfs/logger.h>
#include <dwarfs/writer/categorizer.h>

#include <dwarfs/internal/io_ops.h>
#include <dwarfs/internal/mmap_file_view.h>

using namespace dwarfs;

int main(int argc, char** argv) {
  if (argc < 2) {
    return 1;
  }

  null_logger lgr;

  writer::categorizer_registry catreg;
  auto catmgr = std::make_shared<writer::categorizer_manager>(
      lgr, std::filesystem::path{});
  std::shared_ptr<file_access const> fa = create_file_access_generic();

  boost::program_options::variables_map vm;
  catmgr->add(catreg.create(lgr, "pcmaudio", vm, fa));

  auto const& ops = internal::get_native_memory_mapping_ops();

#ifdef __AFL_LOOP
  while (__AFL_LOOP(10000))
#endif
  {
    std::filesystem::path p(argv[1]);
    auto mm = create_mmap_file_view(ops, p);
    auto job = catmgr->job(p);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm);
    auto res [[maybe_unused]] = job.result();
  }

  return 0;
}
