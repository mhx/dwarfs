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

#include <dwarfs/logger.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/inode_element_view.h>
#include <dwarfs/writer/internal/inode_ordering.h>
#include <dwarfs/writer/internal/promise_receiver.h>
#include <dwarfs/writer/internal/similarity_ordering.h>

namespace dwarfs::writer::internal {

using namespace dwarfs::internal;
namespace fs = std::filesystem;

namespace {

bool inode_less_by_size(inode const* a, inode const* b) {
  auto sa = a->size();
  auto sb = b->size();
  return sa > sb || (sa == sb && a->any()->less_revpath(*b->any()));
}

} // namespace

template <typename LoggerPolicy>
class inode_ordering_ final : public inode_ordering::impl {
 public:
  inode_ordering_(logger& lgr, progress& prog, inode_options const& opts)
      : LOG_PROXY_INIT(lgr)
      , prog_{prog}
      , opts_{opts} {}

  void by_inode_number(sortable_inode_span& sp) const override;
  void by_input_order(sortable_inode_span& sp) const override;
  void by_path(sortable_inode_span& sp) const override;
  void by_reverse_path(sortable_inode_span& sp) const override;
  void
  by_similarity(sortable_inode_span& sp, fragment_category cat) const override;
  void
  by_nilsimsa(worker_group& wg, similarity_ordering_options const& opts,
              sortable_inode_span& sp, fragment_category cat) const override;
  void by_explicit_order(sortable_inode_span& sp, fs::path const& root_path,
                         fragment_order_options const& opts) const override;

 private:
  void
  by_nilsimsa_impl(worker_group& wg, similarity_ordering_options const& opts,
                   std::span<std::shared_ptr<inode> const> inodes,
                   std::vector<uint32_t>& index, fragment_category cat) const;

  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
  inode_options const& opts_;
};

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_inode_number(
    sortable_inode_span& sp) const {
  std::sort(
      sp.index().begin(), sp.index().end(),
      [r = sp.raw()](auto a, auto b) { return r[a]->num() < r[b]->num(); });
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_input_order(
    sortable_inode_span& sp) const {
  std::sort(sp.index().begin(), sp.index().end(),
            [r = sp.raw()](auto a, auto b) {
              return r[a]->any()->order_index() < r[b]->any()->order_index();
            });
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
void inode_ordering_<LoggerPolicy>::by_similarity(sortable_inode_span& sp,
                                                  fragment_category cat) const {
  std::vector<std::optional<uint32_t>> hash_cache;

  auto raw = sp.raw();
  auto& index = sp.index();
  bool any_missing = false;

  hash_cache.resize(raw.size());

  for (auto i : index) {
    auto& cache = hash_cache[i];
    cache = raw[i]->similarity_hash(cat);
    if (!cache.has_value()) {
      any_missing = true;
    }
  }

  auto size_pred = [&](auto a, auto b) {
    return inode_less_by_size(raw[a].get(), raw[b].get());
  };

  auto start = index.begin();

  if (any_missing) {
    start = std::stable_partition(index.begin(), index.end(), [&](auto i) {
      return !hash_cache[i].has_value();
    });

    std::sort(index.begin(), start, size_pred);
  }

  std::sort(start, index.end(), [&](auto a, auto b) {
    assert(hash_cache[a].has_value());
    assert(hash_cache[b].has_value());

    auto const ca = *hash_cache[a];
    auto const cb = *hash_cache[b];

    if (ca < cb) {
      return true;
    }

    if (ca > cb) {
      return false;
    }

    return size_pred(a, b);
  });
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_nilsimsa(
    worker_group& wg, similarity_ordering_options const& opts,
    sortable_inode_span& sp, fragment_category cat) const {
  auto raw = sp.raw();
  auto& index = sp.index();

  if (opts_.max_similarity_scan_size) {
    auto mid = std::stable_partition(index.begin(), index.end(), [&](auto i) {
      return !raw[i]->nilsimsa_similarity_hash(cat);
    });

    if (mid != index.begin()) {
      std::sort(index.begin(), mid, [&](auto a, auto b) {
        return inode_less_by_size(raw[a].get(), raw[b].get());
      });

      if (mid != index.end()) {
        std::vector<uint32_t> small_index(mid, index.end());
        by_nilsimsa_impl(wg, opts, raw, small_index, cat);
        std::copy(small_index.begin(), small_index.end(), mid);
      }

      return;
    }
  }

  by_nilsimsa_impl(wg, opts, raw, index, cat);
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_nilsimsa_impl(
    worker_group& wg, similarity_ordering_options const& opts,
    std::span<std::shared_ptr<inode> const> inodes,
    std::vector<uint32_t>& index, fragment_category cat) const {
  auto ev = inode_element_view(inodes, index, cat);
  std::promise<std::vector<uint32_t>> promise;
  auto future = promise.get_future();
  auto sim_order = similarity_ordering(LOG_GET_LOGGER, prog_, wg, opts);
  sim_order.order_nilsimsa(ev, make_receiver(std::move(promise)),
                           std::move(index));
  future.get().swap(index);
}

template <typename LoggerPolicy>
void inode_ordering_<LoggerPolicy>::by_explicit_order(
    sortable_inode_span& sp, fs::path const& root_path,
    fragment_order_options const& opts) const {
  auto raw = sp.raw();
  auto& index = sp.index();
  auto const& order = opts.explicit_order;

  if (order.empty()) {
    LOG_WARN << "empty explicit order file set";
  }

  std::vector<std::filesystem::path> paths;
  std::vector<std::optional<size_t>> path_order;
  paths.resize(raw.size());
  path_order.resize(raw.size());

  for (auto i : index) {
    paths[i] = raw[i]->any()->fs_path().lexically_relative(root_path);

    if (auto it = order.find(paths[i]); it != order.end()) {
      path_order[i] = it->second;
    } else {
      LOG_DEBUG << "explicit order: " << paths[i]
                << " not found in explicit order file";
    }
  }

  std::sort(index.begin(), index.end(), [&](auto a, auto b) {
    auto const& ai = path_order[a];
    auto const& bi = path_order[b];
    return ai.has_value() && bi.has_value() ? *ai < *bi
                                            : raw[a]->num() < raw[b]->num();
  });

  for (auto i : index) {
    LOG_DEBUG << "explicit order: " << paths[i];
  }
}

inode_ordering::inode_ordering(logger& lgr, progress& prog,
                               inode_options const& opts)
    : impl_(make_unique_logging_object<impl, internal::inode_ordering_,
                                       logger_policies>(lgr, prog, opts)) {}

} // namespace dwarfs::writer::internal
