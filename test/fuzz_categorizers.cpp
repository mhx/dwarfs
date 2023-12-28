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

#include <filesystem>

#include <boost/program_options.hpp>

#include "dwarfs/categorizer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"

using namespace dwarfs;

int main(int argc, char** argv) {
  if (argc < 2) {
    return 1;
  }

  null_logger lgr;

  auto& catreg = categorizer_registry::instance();
  auto catmgr = std::make_shared<categorizer_manager>(lgr);

  boost::program_options::variables_map vm;
  catmgr->add(catreg.create(lgr, "pcmaudio", vm));

#ifdef __AFL_LOOP
  while (__AFL_LOOP(10000))
#endif
  {
    std::filesystem::path p(argv[1]);
    auto mm = mmap(p);
    auto job = catmgr->job(p);
    job.set_total_size(mm.size());
    job.categorize_random_access(mm.span());
    auto res [[maybe_unused]] = job.result();
  }

  return 0;
}
