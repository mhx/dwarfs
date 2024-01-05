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

#include <clocale>
#include <cstdlib>
#include <iostream>

#include <folly/String.h>
#include <folly/experimental/symbolizer/SignalHandler.h>

#include "dwarfs/error.h"
#include "dwarfs/safe_main.h"
#include "dwarfs/terminal.h"
#include "dwarfs/util.h"

namespace dwarfs {

int safe_main(std::function<int(void)> fn) {
  try {
#ifndef _WIN32
    folly::symbolizer::installFatalSignalHandler();
#endif
    setup_default_locale();
    terminal::setup();

    return fn();
  } catch (...) {
    std::cerr << "ERROR: " << folly::exceptionStr(std::current_exception())
              << "\n";
    dump_exceptions();
  }
  return 1;
}

} // namespace dwarfs
