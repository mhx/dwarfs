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

#pragma once

#include <memory>

#include <dwarfs/writer/internal/sortable_inode_span.h>

namespace dwarfs::writer::internal {

namespace detail {

using index_value_t = sortable_inode_span::index_type::value_type;

class idx_less {
 public:
  template <typename F>
  explicit idx_less(F const& f)
      : ctx_{std::addressof(f)}
      , fn_{[](void const* c, index_value_t a, index_value_t b) {
        return (*static_cast<F const*>(c))(a, b);
      }} {}

  bool operator()(index_value_t a, index_value_t b) const {
    return fn_(ctx_, a, b);
  }

 private:
  void const* ctx_;
  bool (*fn_)(void const*, index_value_t, index_value_t);
};

void sort_index_erased(sortable_inode_span::index_type::iterator first,
                       sortable_inode_span::index_type::iterator last,
                       idx_less less);

} // namespace detail

template <typename F>
void sort_index(sortable_inode_span::index_type::iterator first,
                sortable_inode_span::index_type::iterator last, F const& f) {
  detail::sort_index_erased(first, last, detail::idx_less{f});
}

template <typename F>
void sort_index(sortable_inode_span::index_type& idx, F const& f) {
  sort_index(idx.begin(), idx.end(), f);
}

} // namespace dwarfs::writer::internal
