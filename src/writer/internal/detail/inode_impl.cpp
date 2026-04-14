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
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/thrift_lite/demangle.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/detail/inode_impl.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/scanner_progress.h>
#include <dwarfs/writer/internal/similarity.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal::detail {

namespace {

constexpr std::string_view const kScanContext{"[scanning] "};
constexpr std::string_view const kCategorizeContext{"[categorizing] "};

} // namespace

inode_impl::inode_impl() = default;
inode_impl::~inode_impl() = default;

void inode_impl::set_num(uint32_t num) {
  DWARFS_CHECK((flags_ & kNumIsValid) == 0,
               "attempt to set inode number multiple times");
  num_ = num;
  flags_ |= kNumIsValid;
}

uint32_t inode_impl::num() const {
  DWARFS_CHECK((flags_ & kNumIsValid) != 0, "inode number is not set");
  return num_;
}

bool inode_impl::has_category(fragment_category cat) const {
  DWARFS_CHECK(!fragments_.empty(), "has_category() called with no fragments");
  return std::ranges::any_of(
      fragments_, [cat](auto const& f) { return f.category() == cat; });
}

std::optional<uint32_t>
inode_impl::similarity_hash(fragment_category cat) const {
  if (auto sim = find_similarity<uint32_t>(cat)) {
    return *sim;
  }
  return std::nullopt;
}

nilsimsa::hash_type const*
inode_impl::nilsimsa_similarity_hash(fragment_category cat) const {
  return find_similarity<nilsimsa::hash_type>(cat);
}

void inode_impl::populate(file_size_t size) {
  assert(fragments_.empty());
  fragments_.emplace_back(categorizer_manager::default_category(), size);
}

void inode_impl::scan(file_view const& mm, inode_options const& opts,
                      progress& prog) {
  assert(fragments_.empty());

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

      fragments_ = catjob.result();

      if (fragments_.size() > 1) {
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
  if (fragments_.size() <= 1) {
    file_size_t size = mm ? mm.size() : 0;
    if (fragments_.empty()) {
      populate(size);
    }
    auto const chunk_size = prog.similarity.chunk_size.load();
    auto sp = make_progress_context(kScanContext, mm, prog, 4 * chunk_size);
    progress::scan_updater supd(prog.similarity, size);
    scan_full(mm, sp.get(), opts, chunk_size);
  }
}

file_size_t
inode_impl::size(entry_storage& storage, file_id_vector const& files) const {
  return any(storage, files).size();
}

const_file_handle
inode_impl::any(entry_storage& storage, file_id_vector const& files) const {
  DWARFS_CHECK(!files.empty(), "inode has no file (any)");
  for (auto const& f : files) {
    auto fh = storage.handle(f);
    if (!fh.is_invalid()) {
      return fh;
    }
  }
  return storage.handle(files.front());
}

bool inode_impl::append_chunks_to(
    std::vector<chunk_type>& vec,
    std::optional<inode_hole_mapper>& hole_mapper) const {
  for (auto const& frag : fragments_) {
    if (!frag.chunks_are_consistent()) {
      return false;
    }
  }
  for (auto const& frag : fragments_) {
    for (auto const& src : frag.chunks()) {
      auto& chk = vec.emplace_back();
      if (src.is_hole()) {
        DWARFS_CHECK(hole_mapper.has_value(),
                     "inode has hole chunk but there's no hole mapper");
        auto& hm = hole_mapper.value();
        hm.map_hole(chk, src.size());
      } else {
        chk.block() = src.block();
        chk.offset() = src.offset();
        chk.size() = src.size();
      }
    }
  }
  return true;
}

inode_fragments& inode_impl::fragments() { return fragments_; }
inode_fragments const& inode_impl::fragments() const { return fragments_; }

void inode_impl::dump(entry_storage& storage, std::ostream& os,
                      inode_options const& options,
                      file_id_vector const& files) const {
  auto dump_category = [&os, &options](fragment_category const& cat) {
    if (options.categorizer_mgr) {
      os << "[" << options.categorizer_mgr->category_name(cat.value());
      if (cat.has_subcategory()) {
        os << "/" << cat.subcategory();
      }
      os << "] ";
    }
  };

  std::string ino_num{"?"};

  if (flags_ & kNumIsValid) {
    ino_num = std::to_string(num());
  }

  os << "inode " << ino_num << " (" << size(storage, files) << " bytes):\n";
  os << "  files:\n";

  for (auto const& f : files) {
    auto fh = storage.handle(f);
    os << "    " << fh.path_as_string();
    if (fh.is_invalid()) {
      os << " (invalid)";
    }
    os << "\n";
  }

  os << "  fragments:\n";

  for (auto const& f : fragments_.span()) {
    os << "    ";
    dump_category(f.category());
    os << "(" << f.size() << " bytes)\n";
    for (auto const& c : f.chunks()) {
      os << "      (" << c.block() << ", " << c.offset() << ", " << c.size()
         << ")\n";
    }
  }

  os << "  similarity: ";

  auto basic_hash_matcher = [&os](uint32_t sh) {
    os << fmt::format("basic ({0:08x})\n", sh);
  };

  auto nilsimsa_hash_matcher = [&os](nilsimsa::hash_type const& nh) {
    os << fmt::format("nilsimsa ({0:016x}{1:016x}{2:016x}{3:016x})\n", nh[0],
                      nh[1], nh[2], nh[3]);
  };

  auto similarity_map_matcher = [&](similarity_map_type const& map) {
    os << "map\n";
    for (auto const& [cat, val] : map) {
      os << "    ";
      dump_category(cat);
      val | match{
                basic_hash_matcher,
                nilsimsa_hash_matcher,
            };
    }
  };

  similarity_ | match{
                    [&os](std::monostate const&) { os << "none\n"; },
                    basic_hash_matcher,
                    nilsimsa_hash_matcher,
                    similarity_map_matcher,
                };
}

void inode_impl::set_scan_error(const_file_handle fp, std::exception_ptr ep) {
  assert(!scan_error_);
  scan_error_ =
      std::make_unique<std::pair<const_file_handle, std::exception_ptr>>(
          fp, std::move(ep));
}

std::optional<std::pair<const_file_handle, std::exception_ptr>>
inode_impl::get_scan_error() const {
  if (scan_error_) {
    return *scan_error_;
  }
  return std::nullopt;
}

auto inode_impl::mmap_any(entry_storage& storage, os_access const& os,
                          open_file_options const& of_opts,
                          file_id_vector const& files) const
    -> inode_mmap_any_result {
  file_view mm;
  const_file_handle rfh;
  std::vector<std::pair<const_file_handle, std::exception_ptr>> errors;

  for (auto fp : files) {
    auto fh = storage.handle(fp);
    if (!fh.is_invalid()) {
      try {
        mm = os.open_file_with_options(fh.fs_path(), of_opts);
        if (mm.size() != fh.size()) {
          auto const now_size = mm.size();
          mm.reset();
          throw std::runtime_error(fmt::format(
              "file size changed: was {}, now {}", fh.size(), now_size));
        }
        rfh = fh;
        break;
      } catch (...) {
        fh.set_invalid();
        errors.emplace_back(fh, std::current_exception());
      }
    }
  }

  return {std::move(mm), rfh, std::move(errors)};
}

std::shared_ptr<scanner_progress>
inode_impl::make_progress_context(std::string_view context, file_view const& mm,
                                  progress& prog, size_t min_size) const {
  if (mm) {
    if (auto size = mm.size(); std::cmp_greater_equal(size, min_size)) {
      return prog.create_context<scanner_progress>(
          context, path_to_utf8_string_sanitized(mm.path()), size);
    }
  }
  return nullptr;
}

template <typename T>
T const* inode_impl::find_similarity(fragment_category cat) const {
  DWARFS_CHECK(!fragments_.empty(), fmt::format("inode has no fragments ({})",
                                                thrift_lite::demangle<T>()));
  if (std::holds_alternative<std::monostate>(similarity_)) {
    return nullptr;
  }
  if (fragments_.size() == 1) {
    DWARFS_CHECK(
        fragments_.get_single_category() == cat,
        fmt::format("category mismatch ({})", thrift_lite::demangle<T>()));
    return &std::get<T>(similarity_);
  }
  auto& m = std::get<similarity_map_type>(similarity_);
  if (auto it = m.find(cat); it != m.end()) {
    return &std::get<T>(it->second);
  }
  return nullptr;
}

void inode_impl::scan_range(
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

void inode_impl::scan_range(
    file_view const& mm, scanner_progress* sprog, size_t chunk_size,
    std::invocable<std::span<uint8_t const>> auto&& scanner, scan_mode mode) {
  scan_range(mm, sprog, 0, mm.size(), chunk_size,
             std::forward<decltype(scanner)>(scanner), mode);
}

void inode_impl::scan_fragments(file_view const& mm, scanner_progress* sprog,
                                inode_options const& opts, size_t chunk_size) {
  assert(mm);
  assert(fragments_.size() > 1);

  std::unordered_map<fragment_category, similarity> sc;
  std::unordered_map<fragment_category, nilsimsa> nc;

  for (auto [cat, size] : fragments_.get_category_sizes()) {
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

  for (auto const& f : fragments_.span()) {
    auto const size = f.size();

    if (auto i = sc.find(f.category()); i != sc.end()) {
      scan_range(mm, sprog, pos, size, chunk_size, i->second);
    } else if (auto i = nc.find(f.category()); i != nc.end()) {
      scan_range(mm, sprog, pos, size, chunk_size, i->second);
    }

    pos += size;
  }

  similarity_map_type tmp_map;

  for (auto const& [cat, hasher] : sc) {
    tmp_map.emplace(cat, hasher.finalize());
  }

  for (auto const& [cat, hasher] : nc) {
    // TODO: can we finalize in-place?
    nilsimsa::hash_type hash;
    hasher.finalize(hash);
    tmp_map.emplace(cat, hash);
  }

  similarity_.emplace<similarity_map_type>(std::move(tmp_map));
}

void inode_impl::scan_full(file_view const& mm, scanner_progress* sprog,
                           inode_options const& opts, size_t chunk_size) {
  assert(fragments_.size() <= 1);

  if (mm) {
    if (auto max = opts.max_similarity_scan_size;
        max && std::cmp_greater(mm.size(), *max)) {
      return;
    }
  }

  auto order_mode =
      fragments_.empty()
          ? opts.fragment_order.get().mode
          : opts.fragment_order.get(fragments_.get_single_category()).mode;

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
    similarity_.emplace<uint32_t>(sc.finalize());
  } break;

  case fragment_order_mode::NILSIMSA: {
    nilsimsa nc;
    if (mm) {
      scan_range(mm, sprog, chunk_size, nc);
    }
    // TODO: can we finalize in-place?
    nilsimsa::hash_type hash;
    nc.finalize(hash);
    similarity_.emplace<nilsimsa::hash_type>(hash);
  } break;
  }
}

} // namespace dwarfs::writer::internal::detail
