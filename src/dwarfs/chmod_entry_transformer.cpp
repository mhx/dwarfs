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

#include "dwarfs/chmod_entry_transformer.h"
#include "dwarfs/chmod_transformer.h"
#include "dwarfs/entry_interface.h"

namespace dwarfs {

namespace {

class chmod_entry_transformer : public entry_transformer {
 public:
  chmod_entry_transformer(std::string_view spec, file_stat::mode_type umask)
      : transformer_{spec, umask} {}

  void transform(entry_interface& ei) override {
    if (auto perm =
            transformer_.transform(ei.get_permissions(), ei.is_directory())) {
      ei.set_permissions(perm.value());
    }
  }

 private:
  chmod_transformer transformer_;
};

} // namespace

std::unique_ptr<entry_transformer>
create_chmod_entry_transformer(std::string_view spec,
                               file_stat::mode_type umask) {
  return std::make_unique<chmod_entry_transformer>(spec, umask);
}

} // namespace dwarfs
