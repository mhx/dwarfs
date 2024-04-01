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

#include <set>
#include <string>

namespace dwarfs {

enum class version_format {
  maj_min_patch_dec_100, // 1.2.3 <-> 10203
};

class library_dependencies {
 public:
  static std::string common_as_string();

  void add_library(std::string const& name_version_string);
  void add_library(std::string const& library_name,
                   std::string const& version_string);
  void add_library(std::string const& library_name, uint64_t version,
                   version_format fmt);
  void add_library(std::string const& library_name, unsigned major,
                   unsigned minor, unsigned patch);

  void add_common_libraries();

  std::string as_string() const;
  std::set<std::string> const& as_set() const { return deps_; }

 private:
  std::set<std::string> deps_;
};

} // namespace dwarfs
