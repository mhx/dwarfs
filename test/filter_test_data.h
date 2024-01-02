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

#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "dwarfs/filter_debug.h"

namespace dwarfs::test {

class filter_test_data {
 public:
  filter_test_data(std::string_view test_name, std::string_view filter,
                   std::unordered_set<std::string> expected_files)
      : test_name_{test_name}
      , filter_{filter}
      , expected_files_{expected_files} {}

  std::string const& test_name() const { return test_name_; }
  std::string const& filter() const { return filter_; }
  std::unordered_set<std::string> const& expected_files() const {
    return expected_files_;
  }

  std::string get_expected_filter_output(debug_filter_mode mode) const;

 private:
  std::string test_name_;
  std::string filter_;
  std::unordered_set<std::string> expected_files_;
};

std::vector<filter_test_data> const& get_filter_tests();

std::ostream& operator<<(std::ostream& os, filter_test_data const& data);

} // namespace dwarfs::test
