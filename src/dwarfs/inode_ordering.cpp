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
#include "dwarfs/inode_element_view.h"
#include "dwarfs/inode_ordering.h"
#include "dwarfs/logger.h"
#include "dwarfs/promise_receiver.h"
#include "dwarfs/similarity_ordering.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

template <typename LoggerPolicy>
class inode_ordering_ final : public inode_ordering::impl {
 public:
  inode_ordering_(logger& lgr, progress& prog)
      : LOG_PROXY_INIT(lgr)
      , prog_{prog} {}

  void by_inode_number(sortable_inode_span& sp) const override;
  void by_path(sortable_inode_span& sp) const override;
  void by_reverse_path(sortable_inode_span& sp) const override;
  void by_similarity(sortable_inode_span& sp,
                     std::optional<fragment_category> cat) const override;
  void by_nilsimsa(worker_group& wg, similarity_ordering_options const& opts,
                   sortable_inode_span& sp,
                   std::optional<fragment_category> cat) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
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

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_reverse_path(
    sortable_inode_span& sp) const {
  auto raw = sp.raw();
  auto& index = sp.index();

  std::sort(index.begin(), index.end(), [&](auto a, auto b) {
    return raw[a]->any()->less_revpath(*raw[b]->any());
  });
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_similarity(
    sortable_inode_span& sp, std::optional<fragment_category> cat) const {
  std::vector<uint32_t> hash_cache;

  auto raw = sp.raw();
  auto& index = sp.index();

  hash_cache.resize(raw.size());

  for (auto i : index) {
    if (cat) {
      hash_cache[i] = raw[i]->similarity_hash(*cat);
    } else {
      hash_cache[i] = raw[i]->similarity_hash();
    }
  }

  std::sort(index.begin(), index.end(), [&](auto a, auto b) {
    auto const ca = hash_cache[a];
    auto const cb = hash_cache[b];

    if (ca < cb) {
      return true;
    }

    if (ca > cb) {
      return false;
    }

    auto ia = raw[a].get();
    auto ib = raw[b].get();

    return ia->size() > ib->size() ||
           (ia->size() == ib->size() && ia->any()->less_revpath(*ib->any()));
  });
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_nilsimsa(
    worker_group& wg, similarity_ordering_options const& opts,
    sortable_inode_span& sp, std::optional<fragment_category> cat) const {
  inode_element_view ev;
  if (cat) {
    ev = inode_element_view(sp.raw(), sp.index(), *cat);
  } else {
    ev = inode_element_view(sp.raw());
  }
  std::promise<std::vector<uint32_t>> promise;
  auto future = promise.get_future();
  auto sim_order = similarity_ordering(LOG_GET_LOGGER, prog_, wg, opts);
  sim_order.order_nilsimsa(ev, make_receiver(std::move(promise)),
                           std::move(sp.index()));
  future.get().swap(sp.index());
}

} // namespace

inode_ordering::inode_ordering(logger& lgr, progress& prog)
    : impl_(make_unique_logging_object<impl, inode_ordering_, logger_policies>(
          lgr, prog)) {}

} // namespace dwarfs
