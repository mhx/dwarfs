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
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <numeric>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <folly/Demangle.h>
#include <folly/String.h>
#include <folly/small_vector.h>
#include <folly/sorted_vector_types.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/compiler.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/inode_ordering.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/overloaded.h"
#include "dwarfs/progress.h"
#include "dwarfs/promise_receiver.h"
#include "dwarfs/scanner_progress.h"
#include "dwarfs/script.h"
#include "dwarfs/similarity.h"
#include "dwarfs/similarity_ordering.h"
#include "dwarfs/worker_group.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

namespace {

constexpr std::string_view const kScanContext{"[scanning] "};
constexpr std::string_view const kCategorizeContext{"[categorizing] "};

class inode_ : public inode {
 public:
  using chunk_type = thrift::metadata::chunk;

  inode_() = default;

  void set_num(uint32_t num) override {
    DWARFS_CHECK((flags_ & kNumIsValid) == 0,
                 "attempt to set inode number multiple times");
    num_ = num;
    flags_ |= kNumIsValid;
  }

  uint32_t num() const override {
    DWARFS_CHECK((flags_ & kNumIsValid) != 0, "inode number is not set");
    return num_;
  }

  bool has_category(fragment_category cat) const override {
    DWARFS_CHECK(!fragments_.empty(),
                 "has_category() called with no fragments");
    return std::ranges::any_of(
        fragments_, [cat](auto const& f) { return f.category() == cat; });
  }

  std::optional<uint32_t>
  similarity_hash(fragment_category cat) const override {
    if (auto sim = find_similarity<uint32_t>(cat)) {
      return *sim;
    }
    return std::nullopt;
  }

  nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const override {
    return find_similarity<nilsimsa::hash_type>(cat);
  }

  void set_files(files_vector&& fv) override {
    if (!files_.empty()) {
      DWARFS_THROW(runtime_error, "files already set for inode");
    }

    files_ = std::move(fv);
  }

  void populate(size_t size) override {
    assert(fragments_.empty());
    fragments_.emplace_back(categorizer_manager::default_category(), size);
  }

  void scan(mmif* mm, inode_options const& opts, progress& prog) override {
    assert(fragments_.empty());

    categorizer_job catjob;

    // No job if categorizers are disabled
    if (opts.categorizer_mgr) {
      catjob =
          opts.categorizer_mgr->job(mm ? mm->path().string() : "<no-file>");
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
        catjob.set_total_size(mm->size());
        catjob.categorize_random_access(mm->span());

        if (!catjob.best_result_found()) {
          // We must perform a sequential categorizer scan before scanning the
          // fragments, because the ordering is category-dependent.
          // TODO: we might be able to get away with a single scan if we
          //       optimistically assume the default category and perform
          //       both the sequential scan and the default-category order
          //       scan in parallel
          auto const chunk_size = prog.categorize.chunk_size.load();
          auto sp = make_progress_context(kCategorizeContext, mm, prog,
                                          4 * chunk_size);
          progress::scan_updater supd(prog.categorize, mm->size());
          scan_range(mm, sp.get(), chunk_size, [&catjob](auto span) {
            catjob.categorize_sequential(span);
          });
        }

        fragments_ = catjob.result();

        if (fragments_.size() > 1) {
          auto const chunk_size = prog.similarity.chunk_size.load();
          auto sp =
              make_progress_context(kScanContext, mm, prog, 4 * chunk_size);
          progress::scan_updater supd(prog.similarity, mm->size());
          scan_fragments(mm, sp.get(), opts, chunk_size);
        }
      }
    }

    // Add a fragment if nothing has been added so far. We need a single
    // fragment to store the inode's chunks. This won't use up any resources
    // as a single fragment is stored inline.
    if (fragments_.size() <= 1) {
      size_t size = mm ? mm->size() : 0;
      if (fragments_.empty()) {
        populate(size);
      }
      auto const chunk_size = prog.similarity.chunk_size.load();
      auto sp = make_progress_context(kScanContext, mm, prog, 4 * chunk_size);
      progress::scan_updater supd(prog.similarity, size);
      scan_full(mm, sp.get(), opts, chunk_size);
    }
  }

  size_t size() const override { return any()->size(); }

  file const* any() const override {
    if (files_.empty()) {
      DWARFS_THROW(runtime_error, "inode has no file (any)");
    }
    for (auto const& f : files_) {
      if (!f->is_invalid()) {
        return f;
      }
    }
    return files_.front();
  }

  files_vector const& all() const override { return files_; }

  bool append_chunks_to(std::vector<chunk_type>& vec) const override {
    for (auto const& frag : fragments_) {
      if (!frag.chunks_are_consistent()) {
        return false;
      }
    }
    for (auto const& frag : fragments_) {
      auto chks = frag.chunks();
      if (!chks.empty()) {
        vec.insert(vec.end(), chks.begin(), chks.end());
      }
    }
    return true;
  }

  inode_fragments& fragments() override { return fragments_; }

  void dump(std::ostream& os, inode_options const& options) const override {
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

    os << "inode " << ino_num << " (" << any()->size() << " bytes):\n";
    os << "  files:\n";

    for (auto const& f : files_) {
      os << "    " << f->path_as_string();
      if (f->is_invalid()) {
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
        os << "      (" << c.get_block() << ", " << c.get_offset() << ", "
           << c.get_size() << ")\n";
      }
    }

    os << "  similarity: ";

    auto basic_hash_visitor = [&os](uint32_t sh) {
      os << fmt::format("basic ({0:08x})\n", sh);
    };

    auto nilsimsa_hash_visitor = [&os](nilsimsa::hash_type const& nh) {
      os << fmt::format("nilsimsa ({0:016x}{1:016x}{2:016x}{3:016x})\n", nh[0],
                        nh[1], nh[2], nh[3]);
    };

    auto similarity_map_visitor = [&](similarity_map_type const& map) {
      os << "map\n";
      for (auto const& [cat, val] : map) {
        os << "    ";
        dump_category(cat);
        std::visit(
            overloaded{
                basic_hash_visitor,
                nilsimsa_hash_visitor,
            },
            val);
      }
    };

    std::visit(
        overloaded{
            [&os](std::monostate const&) { os << "none\n"; },
            basic_hash_visitor,
            nilsimsa_hash_visitor,
            similarity_map_visitor,
        },
        similarity_);
  }

  void set_scan_error(file const* fp, std::exception_ptr ep) override {
    assert(!scan_error_);
    scan_error_ = std::make_unique<std::pair<file const*, std::exception_ptr>>(
        fp, std::move(ep));
  }

  std::optional<std::pair<file const*, std::exception_ptr>>
  get_scan_error() const override {
    if (scan_error_) {
      return *scan_error_;
    }
    return std::nullopt;
  }

  std::tuple<std::unique_ptr<mmif>, file const*,
             std::vector<std::pair<file const*, std::exception_ptr>>>
  mmap_any(os_access const& os) const override {
    std::unique_ptr<mmif> mm;
    std::vector<std::pair<file const*, std::exception_ptr>> errors;
    file const* rfp{nullptr};

    for (auto fp : files_) {
      if (!fp->is_invalid()) {
        try {
          mm = os.map_file(fp->fs_path(), fp->size());
          rfp = fp;
          break;
        } catch (...) {
          fp->set_invalid();
          errors.emplace_back(fp, std::current_exception());
        }
      }
    }

    return {std::move(mm), rfp, std::move(errors)};
  }

 private:
  std::shared_ptr<scanner_progress>
  make_progress_context(std::string_view context, mmif* mm, progress& prog,
                        size_t min_size) const {
    if (mm) {
      if (auto size = mm->size(); size >= min_size) {
        return prog.create_context<scanner_progress>(
            context, u8string_to_string(mm->path().u8string()), size);
      }
    }
    return nullptr;
  }

  template <typename T>
  T const* find_similarity(fragment_category cat) const {
    if (fragments_.empty()) [[unlikely]] {
      DWARFS_THROW(runtime_error, fmt::format("inode has no fragments ({})",
                                              folly::demangle(typeid(T))));
    }
    if (std::holds_alternative<std::monostate>(similarity_)) {
      return nullptr;
    }
    if (fragments_.size() == 1) {
      if (fragments_.get_single_category() != cat) [[unlikely]] {
        DWARFS_THROW(runtime_error, fmt::format("category mismatch ({})",
                                                folly::demangle(typeid(T))));
      }
      return &std::get<T>(similarity_);
    }
    auto& m = std::get<similarity_map_type>(similarity_);
    if (auto it = m.find(cat); it != m.end()) {
      return &std::get<T>(it->second);
    }
    return nullptr;
  }

  template <typename T>
  void scan_range(mmif* mm, scanner_progress* sprog, size_t offset, size_t size,
                  size_t chunk_size, T&& scanner) {
    while (size >= chunk_size) {
      scanner(mm->span(offset, chunk_size));
      mm->release_until(offset);
      offset += chunk_size;
      size -= chunk_size;
      if (sprog) {
        sprog->bytes_processed += chunk_size;
      }
    }

    scanner(mm->span(offset, size));
    if (sprog) {
      sprog->bytes_processed += size;
    }
  }

  template <typename T>
  void scan_range(mmif* mm, scanner_progress* sprog, size_t chunk_size,
                  T&& scanner) {
    scan_range(mm, sprog, 0, mm->size(), chunk_size, std::forward<T>(scanner));
  }

  void scan_fragments(mmif* mm, scanner_progress* sprog,
                      inode_options const& opts, size_t chunk_size) {
    assert(mm);
    assert(fragments_.size() > 1);

    std::unordered_map<fragment_category, similarity> sc;
    std::unordered_map<fragment_category, nilsimsa> nc;

    for (auto [cat, size] : fragments_.get_category_sizes()) {
      if (auto max = opts.max_similarity_scan_size;
          max && static_cast<size_t>(size) > *max) {
        continue;
      }

      switch (opts.fragment_order.get(cat).mode) {
      case file_order_mode::NONE:
      case file_order_mode::PATH:
      case file_order_mode::REVPATH:
        break;
      case file_order_mode::SIMILARITY:
        sc.try_emplace(cat);
        break;
      case file_order_mode::NILSIMSA:
        nc.try_emplace(cat);
        break;
      }
    }

    if (sc.empty() && nc.empty()) {
      return;
    }

    file_off_t pos = 0;

    for (auto const& f : fragments_.span()) {
      auto const size = f.length();

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

  void scan_full(mmif* mm, scanner_progress* sprog, inode_options const& opts,
                 size_t chunk_size) {
    assert(fragments_.size() <= 1);

    if (mm) {
      if (auto max = opts.max_similarity_scan_size; max && mm->size() > *max) {
        return;
      }
    }

    auto order_mode =
        fragments_.empty()
            ? opts.fragment_order.get().mode
            : opts.fragment_order.get(fragments_.get_single_category()).mode;

    switch (order_mode) {
    case file_order_mode::NONE:
    case file_order_mode::PATH:
    case file_order_mode::REVPATH:
      break;

    case file_order_mode::SIMILARITY: {
      similarity sc;
      if (mm) {
        scan_range(mm, sprog, chunk_size, sc);
      }
      similarity_.emplace<uint32_t>(sc.finalize());
    } break;

    case file_order_mode::NILSIMSA: {
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

  using similarity_map_type =
      folly::sorted_vector_map<fragment_category,
                               std::variant<nilsimsa::hash_type, uint32_t>>;

  static constexpr uint32_t const kNumIsValid{UINT32_C(1) << 0};

  uint32_t flags_{0};
  uint32_t num_{0};
  inode_fragments fragments_;
  files_vector files_;
  std::unique_ptr<std::pair<file const*, std::exception_ptr>> scan_error_;

  std::variant<
      // in case of no hashes at all
      std::monostate,

      // in case of only a single fragment
      nilsimsa::hash_type, // 32 bytes
      uint32_t,            //  4 bytes

      // in case of multiple fragments
      similarity_map_type // 24 bytes
      >
      similarity_;
};

} // namespace

template <typename LoggerPolicy>
class inode_manager_ final : public inode_manager::impl {
 public:
  inode_manager_(logger& lgr, progress& prog, inode_options const& opts)
      : LOG_PROXY_INIT(lgr)
      , prog_(prog)
      , opts_{opts}
      , inodes_need_scanning_{inodes_need_scanning(opts_)} {}

  std::shared_ptr<inode> create_inode() override {
    auto ino = std::make_shared<inode_>();
    inodes_.push_back(ino);
    return ino;
  }

  size_t count() const override { return inodes_.size(); }

  void for_each_inode_in_order(
      std::function<void(std::shared_ptr<inode> const&)> const& fn)
      const override {
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

    std::sort(rv.info.begin(), rv.info.end(), [](auto const& a, auto const& b) {
      return a.total_size > b.total_size ||
             (a.total_size == b.total_size && a.category < b.category);
    });

    if (opts_.categorizer_mgr) {
      std::sort(rv.categories.begin(), rv.categories.end(),
                [&catmgr = *opts_.categorizer_mgr](auto a, auto b) {
                  return catmgr.deterministic_less(a, b);
                });
    } else {
      std::sort(rv.categories.begin(), rv.categories.end());
    }

    return rv;
  }

  void scan_background(worker_group& wg, os_access const& os,
                       std::shared_ptr<inode> ino, file* p) const override;

  bool has_invalid_inodes() const override;

  void try_scan_invalid(worker_group& wg, os_access const& os) override;

  void dump(std::ostream& os) const override;

  sortable_inode_span sortable_span() const override {
    return sortable_inode_span(inodes_);
  }

  sortable_inode_span
  ordered_span(fragment_category cat, worker_group& wg) const override;

 private:
  void update_prog(std::shared_ptr<inode> const& ino, file const* p) const {
    if (p->size() > 0 && !p->is_invalid()) {
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
      return order.mode == file_order_mode::SIMILARITY ||
             order.mode == file_order_mode::NILSIMSA;
    });
  }

  LOG_PROXY_DECL(LoggerPolicy);
  std::vector<std::shared_ptr<inode>> inodes_;
  progress& prog_;
  inode_options opts_;
  bool const inodes_need_scanning_;
  std::atomic<size_t> mutable num_invalid_inodes_{0};
};

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::scan_background(worker_group& wg,
                                                   os_access const& os,
                                                   std::shared_ptr<inode> ino,
                                                   file* p) const {
  // TODO: I think the size check makes everything more complex.
  //       If we don't check the size, we get the code to run
  //       that ensures `fragments_` is updated. Also, there
  //       should only ever be one empty inode, so the check
  //       doesn't actually make much of a difference.
  if (inodes_need_scanning_ /* && p->size() > 0 */) {
    wg.add_job([this, &os, p, ino = std::move(ino)] {
      auto const size = p->size();
      std::shared_ptr<mmif> mm;

      if (size > 0 && !p->is_invalid()) {
        try {
          mm = os.map_file(p->fs_path(), size);
        } catch (...) {
          p->set_invalid();
          // If this file *was* successfully mapped before, there's a slight
          // chance that there's another file with the same hash. We can only
          // figure this out later when all files have been hashed, so we
          // save the error and try again later (in `try_scan_invalid()`).
          ino->set_scan_error(p, std::current_exception());
          ++num_invalid_inodes_;
          return;
        }
      }

      ino->scan(mm.get(), opts_, prog_);
      update_prog(ino, p);
    });
  } else {
    ino->populate(p->size());
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

      std::vector<std::pair<file const*, std::exception_ptr>> errors;
      auto const& fv = ino->all();

      if (fv.size() > 1) {
        auto [mm, p, err] = ino->mmap_any(os);

        if (mm) {
          LOG_DEBUG << "successfully opened: " << p->path_as_string();

          wg.add_job([this, p, ino, mm = std::move(mm)] {
            ino->scan(mm.get(), opts_, prog_);
            update_prog(ino, p);
          });

          continue;
        }

        errors = std::move(err);
      }

      assert(ino->any()->is_invalid());

      ino->scan(nullptr, opts_, prog_);
      update_prog(ino, ino->any());

      errors.emplace_back(scan_err.value());

      for (auto const& [fp, ep] : errors) {
        LOG_ERROR << "failed to map file \"" << fp->path_as_string()
                  << "\": " << folly::exceptionStr(ep)
                  << ", creating empty inode";
        ++prog_.errors;
      }
    }
  }
}

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::dump(std::ostream& os) const {
  for_each_inode_in_order(
      [this, &os](auto const& ino) { ino->dump(os, opts_); });
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
  case file_order_mode::NONE:
    LOG_VERBOSE << prefix << "keeping inode order";
    break;

  case file_order_mode::PATH: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by path name...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_path(span);
    tv << prefix << span.size() << " inodes ordered";
    break;
  }

  case file_order_mode::REVPATH: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by reverse path name...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_reverse_path(span);
    tv << prefix << span.size() << " inodes ordered";
    break;
  }

  case file_order_mode::SIMILARITY: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes by similarity...";
    auto tv = LOG_CPU_TIMED_VERBOSE;
    order.by_similarity(span, cat);
    tv << prefix << span.size() << " inodes ordered";
    break;
  }

  case file_order_mode::NILSIMSA: {
    LOG_VERBOSE << prefix << "ordering " << span.size()
                << " inodes using nilsimsa similarity...";
    similarity_ordering_options soo;
    soo.context = prefix;
    soo.max_children = opts.nilsimsa_max_children;
    soo.max_cluster_size = opts.nilsimsa_max_cluster_size;
    auto tv = LOG_TIMED_VERBOSE;
    order.by_nilsimsa(wg, soo, span, cat);
    tv << prefix << span.size() << " inodes ordered";
    break;
  }
  }

  return span;
}

inode_manager::inode_manager(logger& lgr, progress& prog,
                             inode_options const& opts)
    : impl_(make_unique_logging_object<impl, inode_manager_, logger_policies>(
          lgr, prog, opts)) {}

} // namespace dwarfs
