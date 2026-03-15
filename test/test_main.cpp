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

#include <iostream>

#include <dwarfs/config.h>
#include <dwarfs/util.h>

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/from_current.hpp>
#endif

#include <gtest/gtest.h>

GTEST_API_ int main(int argc, char** argv) {
  int result{1};

  dwarfs::install_signal_handlers();

#ifdef DWARFS_STACKTRACE_ENABLED
  CPPTRACE_TRY {
#endif
    std::cout << "Running main() from " << __FILE__ << "\n";
    testing::InitGoogleTest(&argc, argv);
#ifdef DWARFS_STACKTRACE_ENABLED
    GTEST_FLAG_SET(catch_exceptions, false);
#endif
    result = RUN_ALL_TESTS();
#ifdef DWARFS_STACKTRACE_ENABLED
  }
  CPPTRACE_CATCH(std::exception const& e) {
    std::cout << "Exception: " << e.what() << std::endl;
    cpptrace::from_current_exception().print();
  }
#endif
  return result;
}
