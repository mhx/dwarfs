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
#include <utility>

#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer::internal {

progress::progress() = default;
progress::~progress() = default;

void progress::add_context(std::shared_ptr<context> const& ctx) const {
  contexts_.lock()->push_back(ctx);
}

auto progress::get_active_contexts() const
    -> std::vector<std::shared_ptr<context>> {
  std::vector<std::shared_ptr<context>> rv;

  rv.reserve(16);

  contexts_.with_lock([&](auto& ctxts) {
    // NOLINTNEXTLINE(modernize-use-ranges)
    ctxts.erase(std::remove_if(ctxts.begin(), ctxts.end(),
                               [&rv](auto& wp) {
                                 if (auto sp = wp.lock()) {
                                   rv.push_back(std::move(sp));
                                   return false;
                                 }
                                 return true;
                               }),
                ctxts.end());
  });

  std::ranges::stable_sort(rv, [](auto const& a, auto const& b) {
    return a->get_priority() > b->get_priority();
  });

  return rv;
}

void progress::set_status_function(status_function_type status_fun) {
  status_fun_.with_lock([&](auto& fun) { fun = std::move(status_fun); });
}

std::string progress::status(size_t max_len) {
  std::string rv;
  status_fun_.with_lock([&](auto& fun) {
    if (fun) {
      rv = fun(*this, max_len);
    }
  });
  return rv;
}

} // namespace dwarfs::writer::internal
