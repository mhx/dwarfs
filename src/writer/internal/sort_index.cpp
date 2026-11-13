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

#include <algorithm>

#include <dwarfs/writer/internal/sort_index.h>

namespace dwarfs::writer::internal::detail {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void sort_index_erased(sortable_inode_span::index_type::iterator first,
                       sortable_inode_span::index_type::iterator last,
                       idx_less less) {
  std::ranges::sort(first, last, less);
}

} // namespace dwarfs::writer::internal::detail
