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

#include <algorithm>

#include "dwarfs/entry.h"
#include "dwarfs/inode_ordering.h"
#include "dwarfs/logger.h"

namespace dwarfs {

template <typename LoggerPolicy>
class inode_ordering_ final : public inode_ordering::impl {
 public:
  inode_ordering_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  void by_inode_number(sortable_inode_span& sp) const override;
  void by_path(sortable_inode_span& sp) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
};

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_inode_number(
    sortable_inode_span& sp) const {
  std::sort(
      sp.index().begin(), sp.index().end(),
      [r = sp.raw()](auto a, auto b) { return r[a]->num() < r[b]->num(); });
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_path(sortable_inode_span& sp) const {
  std::vector<std::string> paths;

  auto raw = sp.raw();
  auto& index = sp.index();

  paths.resize(raw.size());

  for (auto i : index) {
    paths[i] = raw[i]->any()->path_as_string();
  }

  std::sort(index.begin(), index.end(),
            [&](auto a, auto b) { return paths[a] < paths[b]; });
}

inode_ordering::inode_ordering(logger& lgr)
    : impl_(make_unique_logging_object<impl, inode_ordering_, logger_policies>(
          lgr)) {}

} // namespace dwarfs
