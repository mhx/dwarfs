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
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <numeric>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/compiler.h>
#include <dwarfs/error.h>
#include <dwarfs/file_view.h>
#include <dwarfs/logger.h>
#include <dwarfs/open_file_options.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/detail/inode_impl.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/inode_ordering.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/promise_receiver.h>
#include <dwarfs/writer/internal/similarity_ordering.h>

namespace dwarfs::writer::internal {

using namespace dwarfs::internal;
namespace fs = std::filesystem;

template <typename LoggerPolicy>
class inode_manager_ final : public inode_manager::impl {
 public:
  inode_manager_(logger& lgr, entry_storage& storage, progress& prog,
                 fs::path const& root_path, inode_options const& opts,
                 bool list_mode)
      : LOG_PROXY_INIT(lgr)
      , storage_(storage)
      , prog_(prog)
      , root_path_{root_path}
      , opts_{opts}
      , inodes_need_scanning_{inodes_need_scanning(opts_)}
      , list_mode_{list_mode} {}

  inode_ptr create_inode() override {
    inode_ptr ino = storage_.create_inode();
    inodes_.push_back(ino);
    return ino;
  }

  size_t count() const override { return inodes_.size(); }

  void
  for_each_inode_in_order(inode_manager::inode_cb const& fn) const override {
    auto span = sortable_span();
    span.all();
    inode_ordering(LOG_GET_LOGGER, prog_, opts_).by_inode_number(span);
    for (auto const& i : span) {
      fn(i);
    }
  }

  inode_manager::fragment_infos fragment_category_info() const override {
    inode_manager::fragment_infos rv;

    std::unordered_map<fragment_category::value_type, std::pair<size_t, size_t>>
        tmp;

    for (auto const& i : inodes_) {
      if (auto const& fragments = i->fragments(); !fragments.empty()) {
        for (auto const& frag : fragments) {
          auto s = frag.size();
          auto& mv = tmp[frag.category().value()];
          ++mv.first;
          mv.second += s;
          rv.category_size[frag.category()] += s;
          rv.total_size += s;
        }
      }
    }

    rv.info.reserve(tmp.size());

    for (auto const& [k, v] : tmp) {
      rv.info.emplace_back(k, v.first, v.second);
    }

    rv.categories.reserve(rv.category_size.size());

    for (auto cs : rv.category_size) {
      rv.categories.emplace_back(cs.first);
    }

    std::ranges::sort(rv.info, [](auto const& a, auto const& b) {
      return a.total_size > b.total_size ||
             (a.total_size == b.total_size && a.category < b.category);
    });

    if (opts_.categorizer_mgr) {
      std::ranges::sort(rv.categories,
                        [&catmgr = *opts_.categorizer_mgr](auto a, auto b) {
                          return catmgr.deterministic_less(a, b);
                        });
    } else {
      std::ranges::sort(rv.categories);
    }

    return rv;
  }

  void scan_background(worker_group& wg, os_access const& os, inode_ptr ino,
                       file_handle p) const override;

  bool has_invalid_inodes() const override;

  void try_scan_invalid(worker_group& wg, os_access const& os) override;

  void dump(std::ostream& os) const override;

  sortable_inode_span sortable_span() const override {
    return sortable_inode_span(storage_, inodes_);
  }

  sortable_inode_span
  ordered_span(fragment_category cat, worker_group& wg) const override;

  size_t get_max_data_chunk_size() const override;

 private:
  void update_prog(inode_ptr ino, const_file_handle p) const {
    if (p.size() > 0 && !p.is_invalid()) {
      prog_.fragments_found += ino->fragments().size();
    }
    ++prog_.inodes_scanned;
    ++prog_.files_scanned;
  }

  static bool inodes_need_scanning(inode_options const& opts) {
    if (opts.categorizer_mgr) {
      return true;
    }

    return opts.fragment_order.any_is([](auto const& order) {
      return order.mode == fragment_order_mode::SIMILARITY ||
             order.mode == fragment_order_mode::NILSIMSA;
    });
  }

  LOG_PROXY_DECL(LoggerPolicy);
  std::vector<inode_ptr> inodes_;
  entry_storage& storage_;
  progress& prog_;
  fs::path const root_path_;
  inode_options opts_;
  bool const inodes_need_scanning_;
  bool const list_mode_;
  std::atomic<size_t> mutable num_invalid_inodes_{0};
};

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::scan_background(worker_group& wg,
                                                   os_access const& os,
                                                   inode_ptr ino,
                                                   file_handle p) const {
  // TODO: I think the size check makes everything more complex.
  //       If we don't check the size, we get the code to run
  //       that ensures `fragments_` is updated. Also, there
  //       should only ever be one empty inode, so the check
  //       doesn't actually make much of a difference.
  if (inodes_need_scanning_ /* && p->size() > 0 */) {
    wg.add_job([this, &os, p, ino] mutable {
      auto const size = p.size();
      file_view mm;

      if (size > 0 && !p.is_invalid()) {
        try {
          mm = os.open_file(p.fs_path());
        } catch (...) {
          p.set_invalid();
          // If this file *was* successfully mapped before, there's a slight
          // chance that there's another file with the same hash. We can only
          // figure this out later when all files have been hashed, so we
          // save the error and try again later (in `try_scan_invalid()`).
          ino->set_scan_error(p, std::current_exception());
          ++num_invalid_inodes_;
          return;
        }

        if (mm.size() != size) {
          ino->set_scan_error(
              p, std::make_exception_ptr(std::runtime_error(fmt::format(
                     "file size changed: was {}, now {}", size, mm.size()))));
          p.set_invalid();
          ++num_invalid_inodes_;
          return;
        }
      }

      ino->scan(mm, opts_, prog_);
      update_prog(ino, p);
    });
  } else {
    ino->populate(p.size());
    update_prog(ino, p);
  }
}

template <typename LoggerPolicy>
bool inode_manager_<LoggerPolicy>::has_invalid_inodes() const {
  assert(inodes_need_scanning_ || num_invalid_inodes_.load() == 0);
  return num_invalid_inodes_.load() > 0;
}

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::try_scan_invalid(worker_group& wg,
                                                    os_access const& os) {
  LOG_VERBOSE << "trying to scan " << num_invalid_inodes_.load()
              << " invalid inodes...";

  for (auto const& ino : inodes_) {
    if (auto scan_err = ino->get_scan_error()) {
      assert(ino->fragments().empty());

      std::vector<std::pair<const_file_handle, std::exception_ptr>> errors;
      auto const& fv = ino->all();

      if (fv.size() > 1) {
        auto [mm, p, err] = ino->mmap_any(os, {});

        if (mm) {
          LOG_DEBUG << "successfully opened: " << p.path_as_string();

          // TODO: p = p is a workaround for older Clang versions
          wg.add_job([this, p = p, ino, mm = std::move(mm)] {
            ino->scan(mm, opts_, prog_);
            update_prog(ino, p);
          });

          continue;
        }

        errors = std::move(err);
      }

      assert(ino->any().is_invalid());

      ino->scan({}, opts_, prog_);
      update_prog(ino, ino->any());

      errors.emplace_back(scan_err.value());

      for (auto const& [fp, ep] : errors) {
        LOG_ERROR << "failed to map file \"" << fp.path_as_string()
                  << "\": " << exception_str(ep) << ", creating empty inode";
        ++prog_.errors;
      }
    }
  }
}

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::dump(std::ostream& os) const {
  for_each_inode_in_order(
      [this, &os](auto const& ino) { ino.dump(os, opts_); });
}

template <typename LoggerPolicy>
auto inode_manager_<LoggerPolicy>::ordered_span(fragment_category cat,
                                                worker_group& wg) const
    -> sortable_inode_span {
  auto prefix = category_prefix(opts_.categorizer_mgr, cat);
  auto opts = opts_.fragment_order.get(cat);

  auto span = sortable_span();
  span.select([cat](auto const& v) { return v->has_category(cat); });

  inode_ordering order(LOG_GET_LOGGER, prog_, opts_);

  switch (opts.mode) {
  case fragment_order_mode::NONE:
    if (list_mode_) {
      LOG_VERBOSE << prefix << "ordering " << span.size()
                  << " inodes by input list order...";
      auto tv = LOG_CPU_TIMED_VERBOSE;
      order.by_input_order(span);
      tv << prefix << span.size() << " inodes ordered";
    } else {
      LOG_VERBOSE << prefix << "keeping inode order";
    }
    break;

  case fragment_order_mode::PATH: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by path name...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_path(span);
    tv << prefix << span.size() << " inodes ordered";
  } break;

  case fragment_order_mode::REVPATH: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by reverse path name...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_reverse_path(span);
    tv << prefix << span.size() << " inodes ordered";
  } break;

  case fragment_order_mode::SIMILARITY: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by similarity...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_similarity(span, cat);
    tv << prefix << span.size() << " inodes ordered";
  } break;

  case fragment_order_mode::NILSIMSA: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes using nilsimsa similarity...";
    similarity_ordering_options soo;
    soo.context = prefix;
    soo.max_children = opts.nilsimsa_max_children;
    soo.max_cluster_size = opts.nilsimsa_max_cluster_size;
    auto tv = LOG_TIMED_VERBOSE;
    order.by_nilsimsa(wg, soo, span, cat);
    tv << prefix << span.size() << " inodes ordered";
  } break;

  case fragment_order_mode::EXPLICIT: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by explicit order...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_explicit_order(span, root_path_, opts);
    tv << prefix << span.size() << " inodes ordered";
  } break;
  }

  return span;
}

template <typename LoggerPolicy>
size_t inode_manager_<LoggerPolicy>::get_max_data_chunk_size() const {
  file_size_t max_chunk_size{0};

  for (auto const& ino : inodes_) {
    for (auto const& frag : ino->fragments().span()) {
      for (auto const& chk : frag.chunks()) {
        if (chk.is_data()) {
          max_chunk_size = std::max(max_chunk_size, chk.size());
        }
      }
    }
  }

  return max_chunk_size;
}

inode_manager::inode_manager(logger& lgr, entry_storage& storage,
                             progress& prog, fs::path const& root_path,
                             inode_options const& opts, bool list_mode)
    : impl_(make_unique_logging_object<impl, internal::inode_manager_,
                                       logger_policies>(
          lgr, storage, prog, root_path, opts, list_mode)) {}

} // namespace dwarfs::writer::internal
