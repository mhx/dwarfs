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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <dwarfs/portability/jthread.h>
#include <dwarfs/portability/unistd.h>

#include <fmt/format.h>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/compiler.h>
#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/util.h>
#include <dwarfs/version.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/entry_filter.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/internal/directory_walker.h>
#include <dwarfs/internal/ordered_preparation_queue.h>
#include <dwarfs/internal/thread_util.h>
#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/block_manager.h>
#include <dwarfs/writer/internal/file_scanner.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>
#include <dwarfs/writer/internal/fragment_chunkable.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/metadata_builder.h>
#include <dwarfs/writer/internal/metadata_freezer.h>
#include <dwarfs/writer/internal/physical_mapping.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/provisional_entry.h>

namespace dwarfs::writer {

namespace internal {

using namespace dwarfs::internal;
using namespace dwarfs::binary_literals;
using namespace std::chrono_literals;

namespace {

// Matches rsync's --copy-unsafe-links lexical classification.
// The boundary is the current imported physical directory.
bool unsafe_symlink(std::filesystem::path const& target,
                    std::filesystem::path const& link_parent,
                    std::filesystem::path const& boundary) {
  if (target.empty() || target.has_root_path()) {
    return true;
  }

  auto const relative = link_parent.lexically_relative(boundary);

  if (relative.empty() || relative.is_absolute()) {
    return true;
  }

  std::size_t depth = 0;

  for (auto const& part : relative) {
    if (part == "..") {
      return true;
    }
    if (part != "." && !part.empty()) {
      ++depth;
    }
  }

  bool seen_non_parent = false;

  for (auto const& part : target) {
    if (part == "..") {
      if (seen_non_parent || depth == 0) {
        return true;
      }
      --depth;
    } else if (part != "." && !part.empty()) {
      seen_non_parent = true;
      ++depth;
    }
  }

  return false;
}

struct directory_ancestry {
  file_stat::dev_type dev;
  file_stat::ino_type ino;
  std::shared_ptr<directory_ancestry const> parent;
};

bool is_ancestor(file_stat const& st,
                 std::shared_ptr<directory_ancestry const> ancestor) {
  for (; ancestor; ancestor = ancestor->parent) {
    if (ancestor->dev == st.dev() && ancestor->ino == st.ino()) {
      return true;
    }
  }
  return false;
}

struct scan_context {
  entry_id parent;
  std::size_t physical_mapping;
  std::filesystem::path boundary;
  std::shared_ptr<directory_ancestry const> ancestors;
};

class cancellable_latch {
 public:
  explicit cancellable_latch(std::ptrdiff_t n)
      : count_{n} {}

  void count_down() {
    std::lock_guard lock(mx_);
    if (--count_ == 0) {
      cv_.notify_all();
    }
  }

  bool wait() {
    std::unique_lock lock(mx_);
    cv_.wait(lock, [this] { return count_ == 0 || cancelled_; });
    return count_ == 0;
  }

  void cancel() {
    {
      std::lock_guard lock(mx_);
      cancelled_ = true;
    }
    cv_.notify_all();
  }

 private:
  std::mutex mx_;
  std::condition_variable cv_;
  std::ptrdiff_t count_;
  bool cancelled_{false};
};

constexpr file_size_t kFilePrepPrefetchBytes = 64_MiB;

void prefetch_fragments(file_view const& mm, inode_handle ino,
                        fragment_category category) {
  if (!mm.supports_raw_bytes()) {
    return;
  }

  file_size_t remaining = kFilePrepPrefetchBytes;
  file_off_t offset = 0;

  for (auto const frag : ino.fragments_view()) {
    if (remaining == 0) {
      break;
    }

    if (frag.category() == category) {
      auto const length = std::min(frag.size(), remaining);
      std::error_code ec;
      mm.advise(io_advice::willneed, {offset, length}, ec);
      remaining -= length;
    }

    offset += frag.size();
  }
}

constexpr std::string_view kEnvVarDumpFilesRaw{"DWARFS_DUMP_FILES_RAW"};
constexpr std::string_view kEnvVarDumpFilesFinal{"DWARFS_DUMP_FILES_FINAL"};
constexpr std::string_view kEnvVarDumpInodes{"DWARFS_DUMP_INODES"};

class visitor_base : public entry_handle_visitor {
 public:
  void visit(file_handle) override {}
  void visit(link_handle) override {}
  void visit(dir_handle) override {}
  void visit(device_handle) override {}
  void visit(other_handle) override {}
};

class hardlink_handler {
 public:
  explicit hardlink_handler(uint32_t& inode_num)
      : inode_num_{inode_num} {}

  uint32_t get_inode_num(entry_handle p, bool* is_new = nullptr) {
    if (is_new) {
      *is_new = true;
    }

    if (p.num_hard_links() <= 1) {
      return inode_num_++;
    }

    auto const id = p.get_unique_inode_id();

    if (!id) {
      return inode_num_++;
    }

    if (auto it = hardlink_map_.find(*id); it != hardlink_map_.end()) {
      if (is_new) {
        *is_new = false;
      }

      return it->second;
    }

    uint32_t const inode_num = inode_num_++;

    hardlink_map_[*id] = inode_num;

    return inode_num;
  }

 private:
  uint32_t& inode_num_;
  std::unordered_map<unique_inode_id, uint32_t> hardlink_map_;
};

class dir_set_inode_visitor : public visitor_base {
 public:
  explicit dir_set_inode_visitor(uint32_t& inode_num)
      : inode_num_(inode_num) {}

  void visit(dir_handle p) override { p.set_inode_num(inode_num_++); }

 private:
  uint32_t& inode_num_;
};

class link_set_inode_visitor : public visitor_base {
 public:
  explicit link_set_inode_visitor(uint32_t& inode_num)
      : hh_{inode_num} {}

  void visit(link_handle p) override { p.set_inode_num(hh_.get_inode_num(p)); }

 private:
  hardlink_handler hh_;
};

class device_set_inode_visitor : public visitor_base {
 public:
  explicit device_set_inode_visitor(uint32_t& inode_num)
      : hh_{inode_num} {}

  void visit(device_handle p) override {
    bool is_new{false};
    p.set_inode_num(hh_.get_inode_num(p, &is_new));
    if (is_new) {
      dev_ids_.push_back(p.posix_device_id());
    }
  }

  std::vector<uint64_t>& device_ids() { return dev_ids_; }

 private:
  std::vector<uint64_t> dev_ids_;
  hardlink_handler hh_;
};

class pipe_set_inode_visitor : public visitor_base {
 public:
  explicit pipe_set_inode_visitor(uint32_t& inode_num)
      : hh_{inode_num} {}

  void visit(other_handle p) override { p.set_inode_num(hh_.get_inode_num(p)); }

 private:
  hardlink_handler hh_;
};

class save_symlinks_visitor : public visitor_base {
 public:
  explicit save_symlinks_visitor(global_entry_data& data)
      : data_(data) {}

  void visit(link_handle p) override { data_.add_link(p.linkname()); }

 private:
  global_entry_data& data_;
};

class save_directories_visitor : public visitor_base {
 public:
  using dir_id_vector = metadata_builder::dir_id_vector;

  explicit save_directories_visitor(size_t num_directories)
      : directory_ids_(std::bit_width(num_directories), num_directories) {}

  void visit(dir_handle p) override {
    auto const ino = p.inode_num().value();
    directory_ids_.at(ino) = p.id();
  }

  dir_id_vector const& get_directory_ids() { return directory_ids_; }

 private:
  dir_id_vector directory_ids_;
};

class save_shared_files_visitor : public visitor_base {
 public:
  using shared_files_table_type =
      dwarfs::thrift::metadata::metadata::shared_files_table_member_type;

  explicit save_shared_files_visitor(uint32_t inode_begin, uint32_t inode_end,
                                     uint32_t num_unique_files)
      : begin_shared_{inode_begin + num_unique_files}
      , num_unique_{num_unique_files} {
    DWARFS_CHECK(inode_end - inode_begin >= num_unique_files,
                 "inconsistent file count");
    shared_files_.resize(inode_end - begin_shared_);
  }

  void visit(file_handle p) override {
    if (auto ino = p.inode_num().value(); ino >= begin_shared_) {
      auto ufi = p.unique_file_id();
      DWARFS_CHECK(ufi >= num_unique_, "inconsistent file id");
      DWARFS_NOTHROW(shared_files_.at(ino - begin_shared_)) = ufi - num_unique_;
    }
  }

  shared_files_table_type& get_shared_files() { return shared_files_; }

 private:
  uint32_t const begin_shared_;
  uint32_t const num_unique_;
  shared_files_table_type shared_files_;
};

std::string status_string(progress const& p, size_t width) {
  auto cp = p.current.load();
  std::string label, path;

  cp | match{
           [&label, &path](const_file_handle const& fh) {
             label = "scanning: ";
             path = fh.path_as_string();
           },
           [&label, &path](const_inode_handle const& ih) {
             label = "writing: ";
             path = ih.any().path_as_string();
           },
           [](std::monostate) {},
       };

  if (!path.empty()) {
    utf8_sanitize(path);
    shorten_path_string(
        path, static_cast<char>(std::filesystem::path::preferred_separator),
        width - label.size());
  }

  return label + path;
}

struct no_mutex {
  void lock() {}
  void unlock() {}
};

file_stat synthetic_root_stat() {
  file_stat st;
  st.set_dev(0);
  st.set_ino(0);
  st.set_nlink(1);
  st.set_mode(posix_file_type::directory | 0755);
#ifndef _WIN32
  st.set_uid(::geteuid());
  st.set_gid(::getegid());
#else
  st.set_uid(0);
  st.set_gid(0);
#endif
  st.set_rdev(0);
  st.set_size(0);
  st.set_blksize(0);
  st.set_blocks(0);
  st.set_allocated_size(0);
  auto const now = std::time(nullptr);
  st.set_atime(now);
  st.set_mtime(now);
  st.set_ctime(now);
  return st;
}

} // namespace

template <typename LoggerPolicy>
class scanner_ final : public scanner::impl {
 public:
  scanner_(logger& lgr, worker_group& wg, segmenter_factory& sf,
           os_access const& os, scanner_options const& options);

  void add_filter(std::unique_ptr<entry_filter>&& filter) override;

  void scan(filesystem_writer& fs_writer, scan_request const& request,
            writer_progress& wprog) override;

 private:
  struct inspected_entry {
    file_stat stat;
    std::optional<std::filesystem::path> physical_path;

    internal::physical_mapping_type mapping(std::size_t inherited) const {
      if (physical_path) {
        return *physical_path;
      }
      return inherited;
    }
  };

  inspected_entry inspect(std::filesystem::path const& path,
                          file_stat const* metadata,
                          std::filesystem::path const& boundary) const;

  void scan_tree(entry_storage& tree, std::filesystem::path const& path,
                 dir_handle parent, std::size_t physical_mapping,
                 std::filesystem::path const& boundary,
                 bool check_collisions, progress& prog, file_scanner& fs);

  void scan_list(entry_storage& tree, std::filesystem::path const& rootpath,
                 std::span<std::filesystem::path const> list, dir_handle root,
                 progress& prog, file_scanner& fs);

  template <typename Lockable>
  entry_handle
  add_entry(Lockable& mx_walk, entry_storage& tree,
            std::filesystem::path const& path, dir_handle parent,
            progress& prog, file_scanner& fs, bool debug_filter = false,
            file_stat const* metadata = nullptr,
            internal::physical_mapping_type mapping = std::size_t{0},
            bool check_collisions = false);

  entry_handle
  add_entry(entry_storage& tree, std::filesystem::path const& path,
            dir_handle parent, progress& prog, file_scanner& fs,
            bool debug_filter = false, std::size_t physical_mapping = 0) {
    no_mutex mx;
    return add_entry(mx, tree, path, parent, prog, fs, debug_filter, nullptr,
                     physical_mapping);
  }

  void dump_state(std::string_view env_var, std::string_view what,
                  std::shared_ptr<file_access const> const& fa,
                  std::function<void(std::ostream&)> const& dumper) const;

  file_view
  prepare_inode_for_category(inode_handle ino, fragment_category category,
                             progress& prog) const;

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  scanner_options const& options_;
  segmenter_factory& segmenter_factory_;
  os_access const& os_;
  std::vector<std::unique_ptr<entry_filter>> filters_;
  std::unordered_set<std::string> invalid_filenames_;
};

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::add_filter(
    std::unique_ptr<entry_filter>&& filter) {
  filters_.push_back(std::move(filter));
}

template <typename LoggerPolicy>
scanner_<LoggerPolicy>::scanner_(logger& lgr, worker_group& wg,
                                 segmenter_factory& sf, os_access const& os,
                                 scanner_options const& options)
    : LOG_PROXY_INIT(lgr)
    , wg_{wg}
    , options_{options}
    , segmenter_factory_{sf}
    , os_{os} {}

DWARFS_PUSH_WARNING
DWARFS_GCC14_DISABLE_WARNING("-Wnrvo")

template <typename LoggerPolicy>
template <typename Lockable>
entry_handle
scanner_<LoggerPolicy>::add_entry(Lockable& mx_walk, entry_storage& tree,
                                  std::filesystem::path const& path,
                                  dir_handle parent, progress& prog,
                                  file_scanner& fs, bool debug_filter,
                                  file_stat const* metadata,
                                  internal::physical_mapping_type mapping,
                                  bool check_collisions) {
  try {
    internal::provisional_entry ent(
        path, metadata ? *metadata : os_.symlink_info(path), parent,
        std::move(mapping));

    if constexpr (!std::is_same_v<std::filesystem::path::value_type, char>) {
      try {
        auto tmp [[maybe_unused]] = path.filename().u8string();
      } catch (std::system_error const& e) {
        LOG_ERROR << fmt::format(
            R"(invalid file path in "{}", storing as "{}": {})",
            path_to_utf8_string_sanitized(path.parent_path()), ent.name(),
            error_cp_to_utf8(e.what()));

        prog.errors++;

        std::unique_lock lock(mx_walk);
        if (!invalid_filenames_.emplace(path_to_utf8_string_sanitized(path))
                 .second) {
          lock.unlock();
          LOG_ERROR << fmt::format(
              "cannot store \"{}\" as the name already exists", ent.name());
          return {};
        }
      }
    }

    bool const exclude = std::ranges::any_of(filters_, [&ent](auto const& f) {
      return f->filter(ent) == filter_action::remove;
    });

    if (debug_filter) {
      std::lock_guard lock(mx_walk);
      (*options_.debug_filter_function)(exclude, ent);
    }

    if (exclude) {
      if (!debug_filter) {
        LOG_DEBUG << "excluding " << ent.unix_dpath();
      }

      return {};
    }

    switch (ent.type()) {
    case entry_type::E_DEVICE:
      if (!options_.with_devices) {
        return {};
      }
      break;

    case entry_type::E_OTHER:
      if (!options_.with_specials) {
        return {};
      }
      break;

    default:
      break;
    }

    // A collision is determined by the image path. Colliding directories
    // are merged, keeping the first directory's metadata.
    if (check_collisions) {
      if (auto existing = parent.find(path.filename())) {
        if (existing.is_dir() && ent.is_directory()) {
          LOG_WARN << "merging directory '" << ent.logical_path() << "' from '"
                   << path_to_utf8_string_sanitized(path) << "' with '"
                   << path_to_utf8_string_sanitized(existing.fs_path())
                   << "' (keeping first directory metadata)";
          return existing;
        }

        LOG_WARN << "duplicate entry '" << ent.logical_path() << "' from '"
                 << path_to_utf8_string_sanitized(path) << "' conflicts with '"
                 << path_to_utf8_string_sanitized(existing.fs_path())
                 << "' (keeping first entry)";
        return {};
      }
    }

    auto pe = ent.commit(tree);

    switch (pe.type()) {
    case entry_type::E_DIR:
      prog.dirs_found++;
      if (!debug_filter) {
        pe.scan(os_, prog);
      }
      break;

    case entry_type::E_FILE:
      prog.files_found++;

      if (!debug_filter) {
        std::lock_guard lock(mx_walk);
        fs.scan(pe.as_file());
      }
      break;

    case entry_type::E_LINK:
      prog.symlinks_found++;
      if (!debug_filter) {
        pe.scan(os_, prog);
      }
      prog.symlinks_scanned++;
      break;

    case entry_type::E_DEVICE:
    case entry_type::E_OTHER:
      prog.specials_found++;
      if (!debug_filter) {
        pe.scan(os_, prog);
      }
      break;

    default:
      LOG_ERROR << "unsupported entry type: " << static_cast<int>(pe.type())
                << " (" << path_to_utf8_string_sanitized(path) << ")";
      prog.errors++;
      break;
    }

    return pe;
  } catch (std::system_error const& e) {
    LOG_ERROR << fmt::format("error reading entry (path={}): {}",
                             path_to_utf8_string_sanitized(path),
                             exception_str(e));
    prog.errors++;
  }

  return {};
}

DWARFS_POP_WARNING

template <typename LoggerPolicy>
auto scanner_<LoggerPolicy>::inspect(
    std::filesystem::path const& path, file_stat const* metadata,
    std::filesystem::path const& boundary) const -> inspected_entry {
  auto st = metadata ? *metadata : os_.symlink_info(path);

  if (auto ec = st.error()) {
    throw std::system_error(ec, path.string());
  }

  if (st.type() != posix_file_type::symlink ||
      options_.dereference == dereference_mode::none) {
    return {std::move(st), std::nullopt};
  }

  // Resolve the parent independently of the logical tree: it may itself be
  // a directory symlink, or a merged directory from a different source.
  auto const parent = os_.canonical(path.parent_path());

  if (options_.dereference == dereference_mode::external_symlinks &&
      !unsafe_symlink(os_.read_symlink(path), parent, boundary)) {
    return {std::move(st), std::nullopt};
  }

  auto physical_path = os_.canonical(parent / path.filename());
  auto referent = os_.symlink_info(physical_path);

  if (auto ec = referent.error()) {
    throw std::system_error(ec, physical_path.string());
  }

  // Dereferencing must not synthesize hardlinks merely because two symlinks
  // happen to refer to the same source inode. File contents can still dedupe.
  referent.set_nlink(1);

  return {std::move(referent), std::move(physical_path)};
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::dump_state(
    std::string_view env_var, std::string_view what,
    std::shared_ptr<file_access const> const& fa,
    std::function<void(std::ostream&)> const& dumper) const {
  if (auto dumpfile = os_.getenv(env_var)) {
    if (fa) {
      LOG_VERBOSE << "dumping " << what << " to " << *dumpfile;
      std::error_code ec;
      auto ofs = fa->open_output(*dumpfile, ec);
      if (ec) {
        LOG_ERROR << "cannot open '" << *dumpfile << "': " << ec.message();
      } else {
        dumper(ofs->os());
        ofs->close(ec);
        if (ec) {
          LOG_ERROR << "cannot close '" << *dumpfile << "': " << ec.message();
        }
      }
    } else {
      LOG_ERROR << "cannot dump " << what << ": no file access";
    }
  }
}

template <typename LoggerPolicy>
file_view
scanner_<LoggerPolicy>::prepare_inode_for_category(inode_handle ino,
                                                   fragment_category category,
                                                   progress& prog) const {
  auto f = ino.any();

  if (options_.hollow_filesystem || f.size() == 0 || f.is_invalid()) {
    return {};
  }

  auto [mm, _, errors] = ino.mmap_any(os_);

  if (mm) {
    prefetch_fragments(mm, ino, category);
  } else {
    for (auto const& [fp, e] : errors) {
      LOG_ERROR << "failed to map file " << fp.path_as_string() << ": "
                << exception_str(e) << ", creating empty inode";
      ++prog.errors;
    }

    for (auto const frag : ino.fragments_view()) {
      if (frag.category() == category) {
        --prog.fragments_found;
      }
    }
  }

  return mm;
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan_tree(entry_storage& tree,
                                       std::filesystem::path const& path,
                                       dir_handle parent,
                                       std::size_t physical_mapping,
                                       std::filesystem::path const& boundary,
                                       bool check_collisions, progress& prog,
                                       file_scanner& fs) {
  bool const debug_filter = options_.debug_filter_function.has_value();

  directory_walker walker(os_, {.num_workers = options_.num_walk_workers,
                                .max_pending_entries = 4096,
                                .batch_size = 64,
                                .collect_metadata = true});

  std::shared_ptr<directory_ancestry const> root_ancestors;

  if (options_.dereference != dereference_mode::none) {
    auto const root_stat = os_.symlink_info(os_.canonical(path));

    if (auto ec = root_stat.error()) {
      throw std::system_error(ec, path.string());
    }

    root_ancestors = std::make_shared<directory_ancestry>(
        root_stat.dev(), root_stat.ino(), nullptr);
  }

  no_mutex mx_scanner;
  walker.run(
      path, scan_context{parent.id(), physical_mapping, boundary,
                         std::move(root_ancestors)},
      [&](walk_entry const& entry,
          scan_context const& ctx) -> std::optional<scan_context> {
        if (entry.metadata && !entry.metadata->has_value()) {
          LOG_ERROR << fmt::format("error reading entry (path={}): {}",
                                   path_to_utf8_string_sanitized(entry.path),
                                   entry.metadata->error().message());
          prog.errors++;
          return std::nullopt;
        }

        try {
          auto incoming = inspect(
              entry.path, entry.metadata ? &entry.metadata->value() : nullptr,
              ctx.boundary);

          if (options_.dereference != dereference_mode::none &&
              incoming.stat.is_directory() &&
              is_ancestor(incoming.stat, ctx.ancestors)) {
            LOG_ERROR << "directory cycle at '"
                      << path_to_utf8_string_sanitized(entry.path) << "'";
            ++prog.errors;
            return std::nullopt;
          }

          auto logical_parent = tree.handle(ctx.parent).as_dir();
          auto pe = add_entry(mx_scanner, tree, entry.path, logical_parent, prog,
                              fs, debug_filter, &incoming.stat,
                              incoming.mapping(ctx.physical_mapping),
                              check_collisions);

          if (!pe || !pe.is_dir()) {
            return std::nullopt;
          }

          auto mapping = ctx.physical_mapping;
          auto boundary = ctx.boundary;

          if (incoming.physical_path) {
            // The incoming directory was reached through a dereferenced
            // symlink, so this traversal needs a physical mapping for its
            // children. If the existing logical entry (pe) already maps to
            // the same physical path, we can reuse its mapping. Otherwise,
            // we create a new mapping without modifying pe.
            if (pe.fs_path().lexically_normal() ==
                incoming.physical_path->lexically_normal()) {
              mapping = tree.physical_mapping_id(pe.id());
            } else {
              mapping = tree.add_physical_path_mapping(*incoming.physical_path,
                                                       pe.id());
            }

            boundary = *incoming.physical_path;
          }

          auto ancestors = ctx.ancestors;

          if (options_.dereference != dereference_mode::none) {
            ancestors = std::make_shared<directory_ancestry>(
                incoming.stat.dev(), incoming.stat.ino(), ctx.ancestors);
          }

          return scan_context{pe.id(), mapping, std::move(boundary),
                              std::move(ancestors)};
        } catch (std::system_error const& e) {
          LOG_ERROR << fmt::format("error following entry (path={}): {}",
                                   path_to_utf8_string_sanitized(entry.path),
                                   exception_str(e));
          ++prog.errors;
          return std::nullopt;
        }
      },
      [&](std::filesystem::path const& dir, scan_context const&,
          std::error_code error) {
        if (error) {
          LOG_ERROR << "cannot read directory '"
                    << path_to_utf8_string_sanitized(dir)
                    << "': " << error.message();
          prog.errors++;
        } else {
          prog.dirs_scanned++;
        }
      });
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan_list(
    entry_storage& tree, std::filesystem::path const& rootpath,
    std::span<std::filesystem::path const> list, dir_handle root,
    progress& prog, file_scanner& fs) {
  if (!filters_.empty()) {
    DWARFS_THROW(runtime_error, "cannot use filters with file lists");
  }

  auto ti = LOG_TIMED_INFO;

  // Explicit lists use the same mapping rules as recursive traversal, but
  // need to remember boundaries for directories already created by the list.
  std::unordered_map<std::string, std::filesystem::path> boundaries;
  boundaries.emplace(root.logical_path(),
                     options_.dereference == dereference_mode::none
                         ? rootpath
                         : os_.canonical(rootpath));

  auto ensure_path = [this, &tree, &prog, &fs, &boundaries](
                         std::filesystem::path const& path, entry_handle root) {
    LOG_TRACE << "ensuring path '" << path_to_utf8_string_sanitized(path)
              << "'";

    for (auto const& component : path) {
      LOG_TRACE << "checking '" << path_to_utf8_string_sanitized(component)
                << "'";
      if (auto d = root.as_dir()) {
        if (auto e = d.find(component.string())) {
          root = e;
        } else {
          LOG_DEBUG << "adding directory '"
                    << path_to_utf8_string_sanitized(component) << "'";

          auto const& boundary = boundaries.at(d.logical_path());
          auto const physical_path = d.fs_path() / component;
          auto incoming = inspect(physical_path, nullptr, boundary);
          no_mutex mx_scanner;

          root = add_entry(mx_scanner, tree, physical_path, d, prog, fs, false,
                           &incoming.stat,
                           incoming.mapping(tree.physical_mapping_id(d.id())),
                           false);

          if (root && root.is_dir()) {
            boundaries.emplace(root.logical_path(),
                               incoming.physical_path.value_or(boundary));
            prog.dirs_scanned++;
          } else {
            DWARFS_THROW(runtime_error,
                         fmt::format("invalid path '{}'",
                                     path_to_utf8_string_sanitized(path)));
          }
        }
      } else {
        DWARFS_THROW(runtime_error,
                     fmt::format("invalid path '{}'",
                                 path_to_utf8_string_sanitized(path)));
      }
    }

    return root;
  };

  std::unordered_map<std::string, dir_handle> dir_cache;
  uint32_t file_order_index{0};

  for (auto const& listpath : list) {
    std::filesystem::path relpath;

    if (listpath.has_root_directory()) {
      if (!rootpath.has_root_directory()) {
        std::string rootpath_str;
        if (rootpath.empty()) {
          rootpath_str = "empty";
        } else {
          rootpath_str = "'" + path_to_utf8_string_sanitized(rootpath) + "'";
        }
        DWARFS_THROW(runtime_error,
                     fmt::format("absolute paths in input list require "
                                 "absolute input path, but input path is {}",
                                 rootpath_str));
      }

      auto mm = std::ranges::mismatch(listpath, rootpath);

      if (mm.in2 == rootpath.end()) {
        relpath = listpath.lexically_relative(rootpath);
      } else {
        LOG_ERROR << "ignoring path '"
                  << path_to_utf8_string_sanitized(listpath)
                  << "' not below input path '"
                  << path_to_utf8_string_sanitized(rootpath) << "'";
        continue;
      }
    } else {
      relpath = listpath.lexically_normal();
    }

    // Remove trailing slash(es) if present, fixes gh #370.
    if (!relpath.has_filename()) {
      relpath = relpath.parent_path();
    }

    auto parent = relpath.parent_path();
    dir_handle pd{};

    LOG_TRACE << "adding path '" << path_to_utf8_string_sanitized(relpath)
              << "' (parent: " << parent
              << ", root: " << path_to_utf8_string_sanitized(rootpath)
              << ", orig: " << path_to_utf8_string_sanitized(listpath) << ")";

    if (auto it = dir_cache.find(parent.string()); it != dir_cache.end()) {
      pd = it->second;
    } else {
      try {
        pd = ensure_path(parent, root).as_dir();
      } catch (std::system_error const& e) {
        LOG_ERROR << "error following input-list directory '"
                  << path_to_utf8_string_sanitized(parent)
                  << "': " << exception_str(e);
        ++prog.errors;
        continue;
      }

      if (pd) {
        dir_cache.emplace(parent.string(), pd);
      } else {
        DWARFS_THROW(runtime_error,
                     fmt::format("invalid path '{}'",
                                 path_to_utf8_string_sanitized(listpath)));
      }
    }

    if (pd.find(relpath)) {
      LOG_WARN << "skipping duplicate entry '"
               << path_to_utf8_string_sanitized(relpath) << "' in input list";
      continue;
    }

    LOG_TRACE << "adding entry '"
              << path_to_utf8_string_sanitized(rootpath / relpath) << "'";

    auto const physical_path = pd.fs_path() / relpath.filename();
    auto const& boundary = boundaries.at(pd.logical_path());
    std::optional<inspected_entry> incoming;

    try {
      incoming.emplace(inspect(physical_path, nullptr, boundary));
    } catch (std::system_error const& e) {
      LOG_ERROR << "error following input-list entry '"
                << path_to_utf8_string_sanitized(physical_path)
                << "': " << exception_str(e);
      ++prog.errors;
      continue;
    }

    no_mutex mx_scanner;

    if (auto pe = add_entry(mx_scanner, tree, physical_path, pd, prog, fs, false,
                            &incoming->stat,
                            incoming->mapping(tree.physical_mapping_id(pd.id())),
                            false)) {
      if (pe.is_dir()) {
        boundaries.emplace(pe.logical_path(),
                           incoming->physical_path.value_or(boundary));
        prog.dirs_scanned++;
      } else if (pe.is_file()) {
        auto fp = pe.as_file();
        fp.set_order_index(file_order_index++);
      }
    }
  }

  ti << "scanned input list";
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan(filesystem_writer& fs_writer,
                                  scan_request const& request,
                                  writer_progress& wprog) {
  if (request.sources.empty()) {
    DWARFS_THROW(runtime_error, "scanner requires at least one source");
  }

  bool const single_source = request.sources.size() == 1;
  bool const list_mode = request.sources.front().input_list.has_value();

  if (!single_source && std::ranges::any_of(request.sources, [](auto const& s) {
        return s.input_list.has_value();
      })) {
    DWARFS_THROW(runtime_error,
                 "cannot combine input lists with multiple sources");
  }

  if (!single_source &&
      options_.inode.fragment_order.any_is([](fragment_order_options const& o) {
        return o.mode == fragment_order_mode::EXPLICIT;
      })) {
    DWARFS_THROW(runtime_error,
                 "cannot use explicit inode ordering with multiple sources");
  }

  auto const& first = request.sources.front();

  if (list_mode && first.mode != scan_source_mode::contents) {
    DWARFS_THROW(runtime_error, "input lists require contents source mode");
  }

  auto const& path = first.path;
  auto const& fa = request.fa;
  auto const& extra_deps = request.extra_deps;

  auto& prog = wprog.get_internal();
  auto& fsw = fs_writer.get_internal();

  if (!options_.debug_filter_function) {
    if (single_source) {
      LOG_INFO << "scanning " << path;
    } else {
      LOG_INFO << "scanning " << request.sources.size() << " sources";
    }
  }

  prog.set_status_function(status_string);

  auto tree = std::make_optional<entry_storage>(options_.metadata);

  inode_manager im(LOG_GET_LOGGER, *tree, prog, path, options_.inode,
                   list_mode);
  auto fs = std::make_optional<file_scanner>(
      LOG_GET_LOGGER, *tree, wg_, os_, im, prog,
      file_scanner::options{
          .hash_files = options_.hash_files,
          .debug_inode_create = os_.getenv(kEnvVarDumpFilesRaw) ||
                                os_.getenv(kEnvVarDumpFilesFinal),
      });

  // All entries, including the root, go through the same entry storage.
  // In the single-source case, preserve the physical root's metadata.
  // Otherwise a synthetic root is used if no metadata was provided.
  auto const first_root_path = [&] {
    if (first.mode == scan_source_mode::contents) {
      return first.path;
    }
    auto parent = first.path.parent_path();
    return parent.empty() ? std::filesystem::path{"."} : parent;
  }();

  auto root_stat = [&] {
    if (request.root_metadata) {
      return *request.root_metadata;
    }
    if (single_source && first.mode == scan_source_mode::contents) {
      auto const root_path = options_.dereference == dereference_mode::none
                                 ? first.path
                                 : os_.canonical(first.path);
      return os_.symlink_info(root_path);
    }
    return synthetic_root_stat();
  }();

  auto const& attrs = request.root_attributes;

  if (attrs.mode) {
    root_stat.set_mode((root_stat.mode() & ~07777u) | *attrs.mode);
  }

  if (attrs.uid) {
    root_stat.set_uid(*attrs.uid);
  }

  if (attrs.gid) {
    root_stat.set_gid(*attrs.gid);
  }

  if (attrs.time) {
    root_stat.set_atime(*attrs.time);
    root_stat.set_mtime(*attrs.time);
    root_stat.set_ctime(*attrs.time);
  }

  auto root = internal::provisional_entry(first_root_path, root_stat)
                  .commit(*tree)
                  .as_dir();

  if (!list_mode) {
    ++prog.dirs_found;
  }

  // A directory walker completes all entry callbacks for a source before
  // returning. Later sources cannot affect first-wins decisions.
  for (std::size_t source_index = 0; source_index < request.sources.size();
       ++source_index) {
    auto const& source = request.sources[source_index];
    bool const check_collisions = source_index != 0;

    if (source.mode == scan_source_mode::entry && !source.path.has_filename()) {
      DWARFS_THROW(runtime_error, "named input sources must have a filename");
    }

    auto const physical_mapping_path = [&] {
      if (source.mode == scan_source_mode::contents) {
        return options_.dereference == dereference_mode::none
                   ? source.path
                   : os_.canonical(source.path);
      }
      auto parent = source.path.parent_path();
      return parent.empty() ? std::filesystem::path{"."} : parent;
    }();
    auto const physical_mapping =
        tree->add_physical_path_mapping(physical_mapping_path, root.id());
    auto parent = root;
    auto active_mapping = physical_mapping;
    auto boundary = physical_mapping_path;

    if (options_.dereference != dereference_mode::none) {
      boundary = os_.canonical(physical_mapping_path);
    }

    if (source.mode == scan_source_mode::entry) {
      no_mutex mx_scanner;
      auto incoming = inspect(source.path, nullptr, boundary);
      auto entry = add_entry(mx_scanner, *tree, source.path, root, prog, *fs,
                             options_.debug_filter_function.has_value(),
                             &incoming.stat, incoming.mapping(physical_mapping),
                             check_collisions);

      if (!entry || !entry.is_dir()) {
        wg_.wait();
        continue;
      }

      parent = entry.as_dir();

      if (incoming.physical_path) {
        if (entry.fs_path().lexically_normal() ==
            incoming.physical_path->lexically_normal()) {
          active_mapping = tree->physical_mapping_id(entry.id());
        } else {
          active_mapping = tree->add_physical_path_mapping(
              *incoming.physical_path, entry.id());
        }

        boundary = *incoming.physical_path;
      }
    } else {
      auto const path = options_.dereference == dereference_mode::none
                            ? source.path
                            : os_.canonical(source.path);

      if (!os_.symlink_info(path).is_directory()) {
        DWARFS_THROW(runtime_error,
                     fmt::format("contents source '{}' is not a directory",
                                 path_to_utf8_string_sanitized(source.path)));
      }
    }

    if (source.input_list) {
      scan_list(*tree, source.path, *source.input_list, parent, prog, *fs);
    } else {
      scan_tree(*tree, source.path, parent, active_mapping, boundary,
                check_collisions, prog, *fs);
    }

    // Finish the current source before continuing with the next one.
    wg_.wait();
  }

  if (options_.debug_filter_function) {
    LOG_VERBOSE << "entry storage:\n" << tree->dump();
    return;
  }

  if (options_.remove_empty_dirs) {
    LOG_INFO << "removing empty directories...";
    tree->remove_empty_dirs(prog);
  }

  LOG_INFO << "waiting for background scanners...";

  wg_.wait();

  // stop progress briefly to avoid race while freezing entry storage
  prog.current.store(std::monostate{});
  prog.set_status_function_and_drain(
      [](progress const&, size_t) { return "freezing entries"; });

  LOG_VERBOSE << "entry storage (before freezing):\n" << tree->dump_entries();

  tree->freeze_entries(LOG_GET_LOGGER, prog);

  auto set_status = [&prog](std::string_view status) {
    prog.set_status_function(
        [msg = std::string(status)](progress const&, size_t) { return msg; });
  };

  LOG_INFO << "scanning CPU time: "
           << time_with_unit(wg_.try_get_cpu_time().value_or(0ns));

  LOG_INFO << "assigning directory and link inodes...";
  set_status("assigning directory and link inodes");

  uint32_t first_link_inode = 0;
  dir_set_inode_visitor dsiv(first_link_inode);
  root.accept(dsiv, true);

  uint32_t first_file_inode = first_link_inode;
  link_set_inode_visitor lsiv(first_file_inode);
  root.accept(lsiv, true);

  dump_state(kEnvVarDumpFilesRaw, "raw files", fa,
             [&fs](auto& os) { fs->dump(os); });

  LOG_INFO << "finalizing file inodes...";
  set_status("finalizing file inodes");
  uint32_t first_device_inode = first_file_inode;
  fs->finalize(first_device_inode);

  dump_state(kEnvVarDumpFilesFinal, "final files", fa,
             [&fs](auto& os) { fs->dump(os); });

  auto const num_unique_files = fs->num_unique();

  fs.reset();

  // this must be done after finalizing the inodes since this is when
  // the file vectors are populated
  if (im.has_invalid_inodes()) {
    LOG_INFO << "trying to recover any invalid inodes...";
    im.try_scan_invalid(wg_, os_);
    wg_.wait();
  }

  tree->drop_file_digests();

  // this step uses quite a bit of memory, so we do it only after having freed
  // up memory used by the file scanner and dropping digests
  tree->compress_path_components(LOG_GET_LOGGER, prog);

  LOG_VERBOSE << "entry storage (after dropping file digests and compressing "
                 "path components):\n"
              << tree->dump_entries();

  LOG_VERBOSE << "inode storage (before freezing):\n" << tree->dump_inodes();

  set_status("freezing inodes");
  tree->freeze_inodes();

  auto original_size = [&] {
    return options_.metadata.enable_sparse_files
               ? prog.allocated_original_size.load()
               : prog.original_size.load();
  };

  auto saved_by_deduplication = [&] {
    return options_.metadata.enable_sparse_files
               ? prog.allocated_saved_by_deduplication.load()
               : prog.saved_by_deduplication.load();
  };

  LOG_INFO << "saved " << size_with_unit(saved_by_deduplication()) << " / "
           << size_with_unit(original_size()) << " ("
           << ratio_to_string(saved_by_deduplication(), original_size())
           << ") in " << prog.duplicate_files << "/" << prog.files_found
           << " duplicate files";

  auto frag_info = im.fragment_category_info();

  if (auto catmgr = options_.inode.categorizer_mgr) {
    for (auto const& ci : frag_info.info) {
      LOG_VERBOSE << "found " << ci.fragment_count << " "
                  << catmgr->category_name(ci.category) << " fragments ("
                  << size_with_unit(ci.total_size) << ")";
    }
  }

  auto ge_data = std::make_optional<global_entry_data>(options_.metadata);
  metadata_builder mdb(LOG_GET_LOGGER, options_.metadata);

  // Ensure that we wait for all background jobs even in case of exceptions
  auto wait_for_background_jobs =
      std::make_optional(scope_exit{[this] { wg_.wait(); }});

  LOG_INFO << "assigning device inodes...";
  set_status("assigning device inodes");
  uint32_t first_pipe_inode = first_device_inode;
  device_set_inode_visitor devsiv(first_pipe_inode);
  root.accept(devsiv);
  mdb.set_devices(std::move(devsiv.device_ids()));

  LOG_INFO << "assigning pipe/socket inodes...";
  set_status("assigning pipe/socket inodes");
  uint32_t last_inode = first_pipe_inode;
  pipe_set_inode_visitor pipsiv(last_inode);
  root.accept(pipsiv);

  mdb.set_symlink_table_size(first_file_inode - first_link_inode);

  wg_.add_job([&] {
    LOG_INFO << "saving symlinks...";
    save_symlinks_visitor ssv(*ge_data);
    root.accept(ssv);

    {
      auto tv = LOG_TIMED_VERBOSE;
      ge_data->update_index();
      tv << "global name and link indices updated";
    }

    LOG_INFO << "updating name and link indices...";
    root.walk([&](entry_handle ep) {
      ep.update(*ge_data);
      if (auto lp = ep.as_link()) {
        mdb.add_symlink_table_entry(
            ep.inode_num().value() - first_link_inode,
            ge_data->get_symlink_table_entry(lp.linkname()));
      }
    });
  });

  dump_state(kEnvVarDumpInodes, "inodes", fa, [&im](auto& os) { im.dump(os); });

  LOG_INFO << "building blocks...";

  prog.set_status_function(status_string);

  // TODO:
  // - get rid of multiple worker groups
  // - instead, introduce "batches" to which work can be added, and
  //   which gets run on a worker groups; each batch keeps track of
  //   its CPU time and affects thread naming

  auto blockmgr = std::make_shared<block_manager>();

  {
    // The latch must outlive every worker group whose jobs may count it down,
    // including when a later step throws while jobs are still running.
    cancellable_latch similarity_latch(frag_info.categories.size());

    size_t const num_threads = options_.num_segmenter_workers;
    worker_group wg_ordering(LOG_GET_LOGGER, os_, "ordering",
                             {.num_workers = num_threads});
    std::unique_ptr<worker_group> wg_fileprep;

    if (options_.num_file_prep_workers > 0 &&
        options_.file_prep_queue_size == 0) {
      DWARFS_THROW(runtime_error, "file prep queue size must be positive");
    }

    if (options_.num_file_prep_workers > 0 && !options_.hollow_filesystem) {
      wg_fileprep = std::make_unique<worker_group>(
          LOG_GET_LOGGER, os_, "fileprep",
          worker_group_options{.num_workers = options_.num_file_prep_workers});
    }

    // Destruct the blockify group before the worker pool it references.
    worker_group wg_blockify(LOG_GET_LOGGER, os_, "blockify",
                             {.num_workers = num_threads});

    fsw.configure(frag_info.categories, num_threads);
    fsw.start_write_filesystem();

    compat::jthread drop_sim_thread([this, &similarity_latch, &tree] {
      set_thread_name("drop-sim-data");
      if (similarity_latch.wait()) {
        tree->drop_similarity_data();
        LOG_VERBOSE << "dropped similarity data from inode storage";
      }
    });

    // In case of an exception, not every category may have been processed,
    // so we need to cancel the latch to avoid deadlock.
    scope_exit cancel_similarity_wait{[&] { similarity_latch.cancel(); }};

    for (auto category : frag_info.categories) {
      auto cat_size = frag_info.category_size.at(category);
      auto catmgr = options_.inode.categorizer_mgr.get();
      std::string meta;

      if (catmgr) {
        meta = catmgr->category_metadata(category);
        if (!meta.empty()) {
          LOG_VERBOSE << category_prefix(catmgr, category)
                      << "metadata: " << meta;
        }
      }

      auto cc = fsw.get_compression_constraints(category.value(), meta);

      LOG_VERBOSE << category_prefix(catmgr, category)
                  << "segmenter will use up to "
                  << size_with_unit(segmenter_factory_.estimate_memory_usage(
                         category, cc));

      wg_blockify.add_job([this, catmgr, blockmgr, category, cat_size, meta, cc,
                           &prog, &fsw, &im, &wg_ordering, &similarity_latch,
                           &wg_fileprep] {
        auto span = im.ordered_span(category, wg_ordering);

        similarity_latch.count_down();

        auto tv = LOG_CPU_TIMED_VERBOSE;

        auto seg = segmenter_factory_.create(
            category, cat_size, cc, blockmgr,
            [category, meta, blockmgr, &fsw](auto block,
                                             auto logical_block_num) {
              fsw.write_block(
                  category, std::move(block),
                  [blockmgr, logical_block_num,
                   category](auto physical_block_num) {
                    blockmgr->set_written_block(logical_block_num,
                                                physical_block_num, category);
                  },
                  meta);
            });

        auto prepared_files = prepare_ordered(
            span, wg_fileprep.get(), options_.file_prep_queue_size,
            [this, category, &prog](inode_handle ino) {
              return prepare_inode_for_category(ino, category, prog);
            });

        for (auto&& prep : prepared_files) {
          // workaround for AppleClang 15
          auto& ino = prep.first;
          auto& mm = prep.second;
          auto f = ino.any();

          if (auto size = f.size(); size > 0 && !f.is_invalid()) {
            prog.current.store(ino);

            auto for_each_fragment = [&](auto&& func) {
              file_off_t offset{0};
              for (auto&& frag : ino.fragments_view()) {
                if (frag.category() == category) {
                  func(frag, offset);
                  prog.fragments_written++;
                }
                offset += frag.size();
              }
            };

            if (options_.hollow_filesystem) {
              for_each_fragment(
                  [&](auto&& frag, auto) { frag.add_hole(frag.size()); });
            } else if (mm) {
              for_each_fragment([&](auto&& frag, auto offset) {
                fragment_chunkable fc(ino, frag, offset, mm, catmgr);
                seg.add_chunkable(fc);
              });
            }
          }

          prog.inodes_written++; // TODO: remove?
        }

        seg.finish();
        fsw.finish_category(category);

        tv << category_prefix(catmgr, category) << "segmenting finished";
      });
    }

    LOG_INFO << "waiting for segmenting/blockifying to finish...";

    // We must wait for blockify first, since the blockify jobs are what
    // trigger the ordering jobs.
    wg_blockify.wait();
    wg_ordering.wait();

    LOG_INFO << "total ordering CPU time: "
             << time_with_unit(wg_ordering.try_get_cpu_time().value_or(0ns));

    LOG_INFO << "total segmenting CPU time: "
             << time_with_unit(wg_blockify.try_get_cpu_time().value_or(0ns));
  }

  wait_for_background_jobs.reset();

  prog.current.store(std::monostate{});
  prog.set_status_function_and_drain(
      [](progress const&, size_t) { return std::string(); });

  mdb.set_block_size(segmenter_factory_.get_block_size());

  LOG_INFO << "saving chunks...";
  set_status("saving chunks");
  mdb.gather_chunks(im, *blockmgr, prog.chunk_count);

  LOG_INFO << "saving directories...";
  set_status("saving directories");
  {
    save_directories_visitor sdv(first_link_inode);
    root.accept(sdv);
    mdb.gather_entries(*tree, sdv.get_directory_ids(), *ge_data, last_inode);
  }

  LOG_INFO << "saving shared files table...";
  set_status("saving shared files table");
  {
    save_shared_files_visitor ssfv(first_file_inode, first_device_inode,
                                   num_unique_files);
    root.accept(ssfv);
    mdb.set_shared_files_table(std::move(ssfv.get_shared_files()));
  }

  if (auto catmgr = options_.inode.categorizer_mgr) {
    std::unordered_map<fragment_category::value_type, uint32_t>
        category_indices;
    std::unordered_map<fragment_category, uint32_t> category_metadata_indices;
    std::vector<std::string> category_names;
    std::vector<std::string> category_metadata;

    category_indices.reserve(frag_info.info.size());
    category_names.reserve(frag_info.info.size());

    for (auto const& ci : frag_info.info) {
      if (category_indices.emplace(ci.category, category_names.size()).second) {
        category_names.emplace_back(catmgr->category_name(ci.category));
      }
    }

    for (auto const& cat : frag_info.categories) {
      auto metadata = catmgr->category_metadata(cat);
      if (!metadata.empty()) {
        if (category_metadata_indices.emplace(cat, category_metadata.size())
                .second) {
          category_metadata.emplace_back(std::move(metadata));
        }
      }
    }

    auto written_categories = blockmgr->get_written_block_categories();
    std::vector<uint32_t> block_categories(written_categories.size());
    std::map<uint32_t, uint32_t> block_cat_metadata;

    std::ranges::transform(
        written_categories, block_categories.begin(),
        [&](auto const& cat) { return category_indices.at(cat.value()); });

    for (auto const& [i, cat] : ranges::views::enumerate(written_categories)) {
      if (auto it = category_metadata_indices.find(cat);
          it != category_metadata_indices.end()) {
        block_cat_metadata.emplace(i, it->second);
      }
    }

    mdb.set_category_names(std::move(category_names));
    mdb.set_block_categories(std::move(block_categories));
    mdb.set_category_metadata_json(std::move(category_metadata));
    mdb.set_block_category_metadata(std::move(block_cat_metadata));
  }

  LOG_VERBOSE << ge_data->to_string();

  set_status("gathering global entry data");
  {
    auto tv = LOG_TIMED_VERBOSE;
    mdb.gather_global_entry_data(*tree, *ge_data);
    tv << "global entry data gathered for metadata";
  }

  // free all entry storage
  LOG_VERBOSE << "entry/inode storage:\n" << tree->dump();
  tree.reset();
  ge_data.reset();

  set_status("building metadata");
  auto metadata = std::make_optional(mdb.build());

  set_status("freezing metadata");
  auto [schema, data] = metadata_freezer(LOG_GET_LOGGER).freeze(*metadata);

  // free memory again
  metadata.reset();

  LOG_VERBOSE << "uncompressed metadata size: " << size_with_unit(data.size());

  fsw.write_metadata_v2_schema(schema);
  fsw.write_metadata_v2(data);

  if (options_.enable_history) {
    history hist(options_.history);
    hist.append(options_.command_line_arguments, extra_deps);
    fsw.write_history(hist.serialize());
  }

  LOG_INFO << "waiting for compression to finish...";
  set_status("waiting for block compression to finish");

  fsw.flush();

  std::string orig_size = size_with_unit(prog.original_size);
  std::string comp_pct =
      ratio_to_string(prog.compressed_size, prog.original_size);

  if (options_.metadata.enable_sparse_files &&
      prog.original_size != prog.allocated_original_size) {
    orig_size += fmt::format(" ({} allocated)",
                             size_with_unit(prog.allocated_original_size));
    if (prog.allocated_original_size > 0) {
      comp_pct += " / " + ratio_to_string(prog.compressed_size,
                                          prog.allocated_original_size);
    }
  }

  LOG_INFO << "compressed " << orig_size << " to "
           << size_with_unit(prog.compressed_size) << " (" << comp_pct << ")";
}

} // namespace internal

scanner::scanner(logger& lgr, thread_pool& pool, segmenter_factory& sf,
                 os_access const& os, scanner_options const& options)
    : impl_(make_unique_logging_object<impl, internal::scanner_,
                                       default_logger_policy>(
          lgr, pool.get_worker_group(), sf, os, options)) {}

} // namespace dwarfs::writer
