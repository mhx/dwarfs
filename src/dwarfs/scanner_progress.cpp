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

#include "dwarfs/scanner_progress.h"

namespace dwarfs {

scanner_progress::scanner_progress(std::string_view context, std::string file,
                                   size_t size)
    : scanner_progress(termcolor::YELLOW, context, file, size) {}

scanner_progress::scanner_progress(termcolor color, std::string_view context,
                                   std::string file, size_t size)
    : color_{color}
    , context_{context}
    , file_{std::move(file)}
    , bytes_total_{size} {}

auto scanner_progress::get_status() const -> status {
  status st;
  st.color = color_;
  st.context = context_;
  st.path.emplace(file_);
  st.bytes_processed.emplace(bytes_processed.load());
  st.bytes_total.emplace(bytes_total_);
  return st;
}

} // namespace dwarfs
