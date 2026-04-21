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

#include <dwarfs/error.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/detail/inode_scanner.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/scanner_progress.h>
#include <dwarfs/writer/internal/similarity.h>

namespace dwarfs::writer::internal::detail {

namespace {

constexpr std::string_view const kScanContext{"[scanning] "};
constexpr std::string_view const kCategorizeContext{"[categorizing] "};

} // namespace

inode_scanner::inode_scanner(entry_storage& storage, inode_id self_id)
    : storage_{&storage}
    , self_id_{self_id} {}

void inode_scanner::populate(file_size_t size) {
  inode_fragments fragments;
  fragments.emplace_back(categorizer_manager::default_category(), size);
  storage_->set_inode_fragments(self_id_, fragments);
}

void inode_scanner::scan(file_view const& mm, inode_options const& opts,
                         progress& prog) {
  inode_fragments fragments;

  categorizer_job catjob;

  // No job if categorizers are disabled
  if (opts.categorizer_mgr) {
    catjob = opts.categorizer_mgr->job(mm ? mm.path().string() : "<no-file>");
  }

  /// TODO: remove comments or move elsewhere
  ///
  /// 1. Run random access categorizers
  /// 2. If we *have* a best category already (need a call for that),
  ///    we can immediately compute similarity hashes for all fragments
  ///    (or not, if the category is configured not to use similarity)
  /// 3. If we *don't* have a best category yet, we can run similarity
  ///    hashing while running the sequential categorizer(s).
  /// 4. If we end up with multiple fragments after all, we might have
  ///    to re-run similarity hashing. This means we can also drop the
  ///    multi-fragment sequential categorizer check, as we can just
  ///    as well support that case.
  ///

  // If we don't have a mapping, we can't scan anything
  if (mm) {
    if (catjob) {
      // First, run random access categorizers. If we get a result here,
      // it's very likely going to be the best result.
      catjob.set_total_size(mm.size());
      catjob.categorize_random_access(mm);

      if (!catjob.best_result_found()) {
        // We must perform a sequential categorizer scan before scanning the
        // fragments, because the ordering is category-dependent.
        // TODO: we might be able to get away with a single scan if we
        //       optimistically assume the default category and perform
        //       both the sequential scan and the default-category order
        //       scan in parallel
        auto const chunk_size = prog.categorize.chunk_size.load();
        auto sp =
            make_progress_context(kCategorizeContext, mm, prog, 4 * chunk_size);
        progress::scan_updater supd(prog.categorize, mm.size());
        catjob.categorize_sequential(mm, chunk_size, sp.get());
      }

      fragments = catjob.result();

      if (!fragments.empty()) {
        storage_->set_inode_fragments(self_id_, fragments);
      }

      if (fragments.size() > 1) {
        auto const chunk_size = prog.similarity.chunk_size.load();
        auto sp = make_progress_context(kScanContext, mm, prog, 4 * chunk_size);
        progress::scan_updater supd(prog.similarity, mm.size());
        scan_fragments(mm, sp.get(), opts, chunk_size);
      }
    }
  }

  // Add a fragment if nothing has been added so far. We need a single
  // fragment to store the inode's chunks. This won't use up any resources
  // as a single fragment is stored inline.
  if (fragments.size() <= 1) {
    file_size_t size = mm ? mm.size() : 0;
    if (fragments.empty()) {
      populate(size);
    }
    auto const chunk_size = prog.similarity.chunk_size.load();
    auto sp = make_progress_context(kScanContext, mm, prog, 4 * chunk_size);
    progress::scan_updater supd(prog.similarity, size);
    scan_full(mm, sp.get(), opts, chunk_size);
  }
}

std::shared_ptr<scanner_progress>
inode_scanner::make_progress_context(std::string_view context,
                                     file_view const& mm, progress& prog,
                                     size_t min_size) const {
  if (mm) {
    if (auto size = mm.size(); std::cmp_greater_equal(size, min_size)) {
      return prog.create_context<scanner_progress>(
          context, path_to_utf8_string_sanitized(mm.path()), size);
    }
  }
  return nullptr;
}

void inode_scanner::scan_range(
    file_view const& mm, scanner_progress* sprog, file_off_t offset,
    file_size_t size, size_t chunk_size,
    std::invocable<std::span<uint8_t const>> auto&& scanner, scan_mode mode) {
  auto&& scan = std::forward<decltype(scanner)>(scanner);

  auto advance = [&](file_size_t n) {
    if (sprog) {
      sprog->advance(n);
    }
  };

  for (auto const& ext : mm.extents({offset, size})) {
    if (ext.kind() == extent_kind::hole && mode == scan_mode::skip_holes) {
      advance(ext.size());
      continue;
    }

    for (auto const& seg : ext.segments(chunk_size)) {
      scan(seg.span<uint8_t>());
      advance(seg.size());
    }
  }
}

void inode_scanner::scan_range(
    file_view const& mm, scanner_progress* sprog, size_t chunk_size,
    std::invocable<std::span<uint8_t const>> auto&& scanner, scan_mode mode) {
  scan_range(mm, sprog, 0, mm.size(), chunk_size,
             std::forward<decltype(scanner)>(scanner), mode);
}

void inode_scanner::scan_fragments(file_view const& mm, scanner_progress* sprog,
                                   inode_options const& opts,
                                   size_t chunk_size) {
  inode_fragments_view fragments{*storage_, self_id_};

  assert(mm);
  assert(fragments.size() > 1);

  std::unordered_map<fragment_category, similarity> sc;
  std::unordered_map<fragment_category, nilsimsa> nc;

  for (auto [cat, size] : fragments.get_category_sizes()) {
    if (auto max = opts.max_similarity_scan_size;
        max && std::cmp_greater(size, *max)) {
      continue;
    }

    switch (opts.fragment_order.get(cat).mode) {
    case fragment_order_mode::NONE:
    case fragment_order_mode::PATH:
    case fragment_order_mode::REVPATH:
    case fragment_order_mode::EXPLICIT:
      break;
    case fragment_order_mode::SIMILARITY:
      sc.try_emplace(cat);
      break;
    case fragment_order_mode::NILSIMSA:
      nc.try_emplace(cat);
      break;
    }
  }

  if (sc.empty() && nc.empty()) {
    return;
  }

  file_off_t pos = 0;

  for (auto const& f : fragments) {
    auto const size = f.size();

    if (auto i = sc.find(f.category()); i != sc.end()) {
      scan_range(mm, sprog, pos, size, chunk_size, i->second);
    } else if (auto i = nc.find(f.category()); i != nc.end()) {
      scan_range(mm, sprog, pos, size, chunk_size, i->second);
    }

    pos += size;
  }

  std::vector<inode_similarity_hash_data> sim_data;

  sim_data.reserve(sc.size() + nc.size());

  for (auto const& [cat, hasher] : sc) {
    sim_data.emplace_back(cat, hasher.finalize());
  }

  for (auto const& [cat, hasher] : nc) {
    nilsimsa::hash_type value;
    hasher.finalize(value);
    sim_data.emplace_back(cat, value);
  }

  storage_->set_inode_similarity(self_id_, sim_data);
}

void inode_scanner::scan_full(file_view const& mm, scanner_progress* sprog,
                              inode_options const& opts, size_t chunk_size) {
  inode_fragments_view fragments{*storage_, self_id_};

  assert(fragments.size() <= 1);

  if (mm) {
    if (auto max = opts.max_similarity_scan_size;
        max && std::cmp_greater(mm.size(), *max)) {
      return;
    }
  }

  auto const category = fragments.empty()
                            ? categorizer_manager::default_category()
                            : fragments.get_single_category();

  auto const order_mode = fragments.empty()
                              ? opts.fragment_order.get().mode
                              : opts.fragment_order.get(category).mode;

  switch (order_mode) {
  case fragment_order_mode::NONE:
  case fragment_order_mode::PATH:
  case fragment_order_mode::REVPATH:
  case fragment_order_mode::EXPLICIT:
    break;

  case fragment_order_mode::SIMILARITY: {
    similarity sc;
    if (mm) {
      scan_range(mm, sprog, chunk_size, sc);
    }
    auto const value = sc.finalize();
    std::array<inode_similarity_hash_data, 1> sim_data{{{category, value}}};
    storage_->set_inode_similarity(self_id_, sim_data);
  } break;

  case fragment_order_mode::NILSIMSA: {
    nilsimsa nc;
    if (mm) {
      scan_range(mm, sprog, chunk_size, nc);
    }
    nilsimsa::hash_type value;
    nc.finalize(value);
    std::array<inode_similarity_hash_data, 1> sim_data{{{category, value}}};
    storage_->set_inode_similarity(self_id_, sim_data);
  } break;
  }
}

} // namespace dwarfs::writer::internal::detail
