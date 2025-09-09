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
#include <deque>
#include <functional>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <folly/CPortability.h>
#include <folly/portability/Unistd.h>
#include <folly/system/HardwareConcurrency.h>

#include <fmt/format.h>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/util.h>
#include <dwarfs/version.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/entry_filter.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/block_manager.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/file_scanner.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>
#include <dwarfs/writer/internal/fragment_chunkable.h>
#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/inode.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/metadata_builder.h>
#include <dwarfs/writer/internal/metadata_freezer.h>
#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer {

namespace internal {

using namespace dwarfs::internal;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kEnvVarDumpFilesRaw{"DWARFS_DUMP_FILES_RAW"};
constexpr std::string_view kEnvVarDumpFilesFinal{"DWARFS_DUMP_FILES_FINAL"};
constexpr std::string_view kEnvVarDumpInodes{"DWARFS_DUMP_INODES"};

class visitor_base : public entry_visitor {
 public:
  void visit(file*) override {}
  void visit(link*) override {}
  void visit(dir*) override {}
  void visit(device*) override {}
};

class dir_set_inode_visitor : public visitor_base {
 public:
  explicit dir_set_inode_visitor(uint32_t& inode_num)
      : inode_num_(inode_num) {}

  void visit(dir* p) override {
    p->sort();
    p->set_inode_num(inode_num_++);
  }

 private:
  uint32_t& inode_num_;
};

class link_set_inode_visitor : public visitor_base {
 public:
  explicit link_set_inode_visitor(uint32_t& inode_num)
      : inode_num_(inode_num) {}

  void visit(link* p) override { p->set_inode_num(inode_num_++); }

 private:
  uint32_t& inode_num_;
};

class device_set_inode_visitor : public visitor_base {
 public:
  explicit device_set_inode_visitor(uint32_t& inode_num)
      : inode_num_(inode_num) {}

  void visit(device* p) override {
    if (p->type() == entry::E_DEVICE) {
      p->set_inode_num(inode_num_++);
      dev_ids_.push_back(p->device_id());
    }
  }

  std::vector<uint64_t>& device_ids() { return dev_ids_; }

 private:
  std::vector<uint64_t> dev_ids_;
  uint32_t& inode_num_;
};

class pipe_set_inode_visitor : public visitor_base {
 public:
  explicit pipe_set_inode_visitor(uint32_t& inode_num)
      : inode_num_(inode_num) {}

  void visit(device* p) override {
    if (p->type() != entry::E_DEVICE) {
      p->set_inode_num(inode_num_++);
    }
  }

 private:
  uint32_t& inode_num_;
};

class names_and_symlinks_visitor : public visitor_base {
 public:
  explicit names_and_symlinks_visitor(global_entry_data& data)
      : data_(data) {}

  void visit(file* p) override { data_.add_name(p->name()); }

  void visit(device* p) override { data_.add_name(p->name()); }

  void visit(link* p) override {
    data_.add_name(p->name());
    data_.add_link(p->linkname());
  }

  void visit(dir* p) override {
    if (p->has_parent()) {
      data_.add_name(p->name());
    }
  }

 private:
  global_entry_data& data_;
};

class save_directories_visitor : public visitor_base {
 public:
  explicit save_directories_visitor(size_t num_directories) {
    directories_.resize(num_directories);
  }

  void visit(dir* p) override { directories_.at(p->inode_num().value()) = p; }

  std::span<dir*> get_directories() { return directories_; }

 private:
  std::vector<dir*> directories_;
};

class save_shared_files_visitor : public visitor_base {
 public:
  explicit save_shared_files_visitor(uint32_t inode_begin, uint32_t inode_end,
                                     uint32_t num_unique_files)
      : begin_shared_{inode_begin + num_unique_files}
      , num_unique_{num_unique_files} {
    DWARFS_CHECK(inode_end - inode_begin >= num_unique_files,
                 "inconsistent file count");
    shared_files_.resize(inode_end - begin_shared_);
  }

  void visit(file* p) override {
    if (auto ino = p->inode_num().value(); ino >= begin_shared_) {
      auto ufi = p->unique_file_id();
      DWARFS_CHECK(ufi >= num_unique_, "inconsistent file id");
      DWARFS_NOTHROW(shared_files_.at(ino - begin_shared_)) = ufi - num_unique_;
    }
  }

  std::vector<uint32_t>& get_shared_files() { return shared_files_; }

 private:
  uint32_t const begin_shared_;
  uint32_t const num_unique_;
  std::vector<uint32_t> shared_files_;
};

std::string status_string(progress const& p, size_t width) {
  auto cp = p.current.load();
  std::string label, path;

  if (cp) {
    if (auto e = dynamic_cast<entry_interface const*>(cp)) {
      label = "scanning: ";
      path = e->path_as_string();
    } else if (auto i = dynamic_cast<inode const*>(cp)) {
      label = "writing: ";
      path = i->any()->path_as_string();
    }
    utf8_sanitize(path);
    shorten_path_string(
        path, static_cast<char>(std::filesystem::path::preferred_separator),
        width - label.size());
  }

  return label + path;
}

} // namespace

template <typename LoggerPolicy>
class scanner_ final : public scanner::impl {
 public:
  scanner_(logger& lgr, worker_group& wg, segmenter_factory& sf,
           entry_factory& ef, os_access const& os,
           scanner_options const& options);

  void add_filter(std::unique_ptr<entry_filter>&& filter) override;

  void
  scan(filesystem_writer& fs_writer, std::filesystem::path const& path,
       writer_progress& wprog,
       std::optional<std::span<std::filesystem::path const>> list,
       std::shared_ptr<file_access const> fa,
       std::function<void(library_dependencies&)> const& extra_deps) override;

 private:
  entry_factory::node scan_tree(std::filesystem::path const& path,
                                progress& prog, file_scanner& fs);

  entry_factory::node scan_list(std::filesystem::path const& rootpath,
                                std::span<std::filesystem::path const> list,
                                progress& prog, file_scanner& fs);

  entry_factory::node
  add_entry(std::filesystem::path const& name,
            std::shared_ptr<dir> const& parent, progress& prog,
            file_scanner& fs, bool debug_filter = false);

  void dump_state(std::string_view env_var, std::string_view what,
                  std::shared_ptr<file_access const> const& fa,
                  std::function<void(std::ostream&)> const& dumper) const;

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  scanner_options const& options_;
  segmenter_factory& segmenter_factory_;
  entry_factory& entry_factory_;
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
                                 segmenter_factory& sf, entry_factory& ef,
                                 os_access const& os,
                                 scanner_options const& options)
    : LOG_PROXY_INIT(lgr)
    , wg_{wg}
    , options_{options}
    , segmenter_factory_{sf}
    , entry_factory_{ef}
    , os_{os} {}

FOLLY_PUSH_WARNING
FOLLY_GCC_DISABLE_WARNING("-Wnrvo")

template <typename LoggerPolicy>
entry_factory::node
scanner_<LoggerPolicy>::add_entry(std::filesystem::path const& name,
                                  std::shared_ptr<dir> const& parent,
                                  progress& prog, file_scanner& fs,
                                  bool debug_filter) {
  try {
    auto pe = entry_factory_.create(os_, name, parent);

    if constexpr (!std::is_same_v<std::filesystem::path::value_type, char>) {
      try {
        auto tmp [[maybe_unused]] = name.filename().u8string();
      } catch (std::system_error const& e) {
        LOG_ERROR << fmt::format(
            R"(invalid file name in "{}", storing as "{}": {})",
            path_to_utf8_string_sanitized(name.parent_path()), pe->name(),
            error_cp_to_utf8(e.what()));

        prog.errors++;

        if (!invalid_filenames_.emplace(path_to_utf8_string_sanitized(name))
                 .second) {
          LOG_ERROR << fmt::format(
              "cannot store \"{}\" as the name already exists", pe->name());
          return nullptr;
        }
      }
    }

    bool const exclude =
        std::any_of(filters_.begin(), filters_.end(), [&pe](auto const& f) {
          return f->filter(*pe) == filter_action::remove;
        });

    if (debug_filter) {
      (*options_.debug_filter_function)(exclude, *pe);
    }

    if (exclude) {
      if (!debug_filter) {
        LOG_DEBUG << "excluding " << pe->dpath();
      }

      return nullptr;
    }

    switch (pe->type()) {
    case entry::E_FILE:
      if (!debug_filter && pe->size() > 0 && os_.access(pe->fs_path(), R_OK)) {
        LOG_ERROR << "cannot access " << pe->path_as_string()
                  << ", creating empty file";
        pe->override_size(0);
        prog.errors++;
      }
      break;

    case entry::E_DEVICE:
      if (!options_.with_devices) {
        return nullptr;
      }
      break;

    case entry::E_OTHER:
      if (!options_.with_specials) {
        return nullptr;
      }
      break;

    default:
      break;
    }

    parent->add(pe);

    switch (pe->type()) {
    case entry::E_DIR:
      // prog.current.store(pe.get());
      prog.dirs_found++;
      if (!debug_filter) {
        pe->scan(os_, prog);
      }
      break;

    case entry::E_FILE:
      prog.files_found++;
      if (!debug_filter) {
        fs.scan(dynamic_cast<file*>(pe.get()));
      }
      break;

    case entry::E_LINK:
      prog.symlinks_found++;
      if (!debug_filter) {
        pe->scan(os_, prog);
      }
      prog.symlinks_scanned++;
      break;

    case entry::E_DEVICE:
    case entry::E_OTHER:
      prog.specials_found++;
      if (!debug_filter) {
        pe->scan(os_, prog);
      }
      break;

    default:
      LOG_ERROR << "unsupported entry type: " << int(pe->type()) << " ("
                << pe->path_as_string() << ")";
      prog.errors++;
      break;
    }

    return pe;
  } catch (std::system_error const& e) {
    LOG_ERROR << fmt::format("error reading entry (path={}): {}",
                             path_to_utf8_string_sanitized(name),
                             exception_str(e));
    prog.errors++;
  }

  return nullptr;
}

FOLLY_POP_WARNING

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
entry_factory::node
scanner_<LoggerPolicy>::scan_tree(std::filesystem::path const& path,
                                  progress& prog, file_scanner& fs) {
  auto root = entry_factory_.create(os_, path);
  bool const debug_filter = options_.debug_filter_function.has_value();

  if (root->type() != entry::E_DIR) {
    DWARFS_THROW(runtime_error,
                 fmt::format("'{}' must be a directory", path.string()));
  }

  std::deque<entry_factory::node> queue({root});
  prog.dirs_found++;

  while (!queue.empty()) {
    auto parent = std::dynamic_pointer_cast<dir>(queue.front());

    DWARFS_CHECK(parent, "expected directory");

    queue.pop_front();
    auto ppath = parent->fs_path();

    try {
      auto d = os_.opendir(ppath);
      std::filesystem::path name;
      std::vector<entry_factory::node> subdirs;

      while (d->read(name)) {
        if (auto pe = add_entry(name, parent, prog, fs, debug_filter)) {
          if (pe->type() == entry::E_DIR) {
            subdirs.push_back(pe);
          }
        }
      }

      queue.insert(queue.begin(), subdirs.begin(), subdirs.end());

      prog.dirs_scanned++;
    } catch (std::system_error const& e) {
      LOG_ERROR << "cannot read directory '"
                << path_to_utf8_string_sanitized(ppath)
                << "': " << exception_str(e);
      prog.errors++;
    }
  }

  return root;
}

template <typename LoggerPolicy>
entry_factory::node
scanner_<LoggerPolicy>::scan_list(std::filesystem::path const& rootpath,
                                  std::span<std::filesystem::path const> list,
                                  progress& prog, file_scanner& fs) {
  if (!filters_.empty()) {
    DWARFS_THROW(runtime_error, "cannot use filters with file lists");
  }

  auto ti = LOG_TIMED_INFO;

  LOG_DEBUG << "creating root directory '"
            << path_to_utf8_string_sanitized(rootpath) << "'";

  auto root = entry_factory_.create(os_, rootpath);

  if (root->type() != entry::E_DIR) {
    DWARFS_THROW(runtime_error,
                 fmt::format("'{}' must be a directory",
                             path_to_utf8_string_sanitized(rootpath)));
  }

  auto ensure_path = [this, &prog, &fs](std::filesystem::path const& path,
                                        entry_factory::node root) {
    LOG_TRACE << "ensuring path '" << path_to_utf8_string_sanitized(path)
              << "'";

    for (auto const& component : path) {
      LOG_TRACE << "checking '" << path_to_utf8_string_sanitized(component)
                << "'";
      if (auto d = std::dynamic_pointer_cast<dir>(root)) {
        if (auto e = d->find(component.string())) {
          root = e;
        } else {
          LOG_DEBUG << "adding directory '"
                    << path_to_utf8_string_sanitized(component) << "'";
          root = add_entry(d->fs_path() / component, d, prog, fs);
          if (root && root->type() == entry::E_DIR) {
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

  std::unordered_map<std::string, std::shared_ptr<dir>> dir_cache;

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
      relpath = listpath;
    }

    auto parent = relpath.parent_path();
    std::shared_ptr<dir> pd;

    LOG_TRACE << "adding path '" << path_to_utf8_string_sanitized(relpath)
              << "' (parent: " << parent
              << ", root: " << path_to_utf8_string_sanitized(rootpath)
              << ", orig: " << path_to_utf8_string_sanitized(listpath) << ")";

    if (auto it = dir_cache.find(parent.string()); it != dir_cache.end()) {
      pd = it->second;
    } else {
      pd = std::dynamic_pointer_cast<dir>(ensure_path(parent, root));

      if (pd) {
        dir_cache.emplace(parent.string(), pd);
      } else {
        DWARFS_THROW(runtime_error,
                     fmt::format("invalid path '{}'",
                                 path_to_utf8_string_sanitized(listpath)));
      }
    }

    if (auto pe = pd->find(relpath)) {
      LOG_INFO << "skipping duplicate entry '"
               << path_to_utf8_string_sanitized(relpath) << "' in input list";
      continue;
    }

    LOG_TRACE << "adding entry '"
              << path_to_utf8_string_sanitized(rootpath / relpath) << "'";

    if (auto pe = add_entry(rootpath / relpath, pd, prog, fs)) {
      if (pe->type() == entry::E_DIR) {
        prog.dirs_scanned++;
      }
    }
  }

  ti << "scanned input list";

  return root;
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan(
    filesystem_writer& fs_writer, std::filesystem::path const& path,
    writer_progress& wprog,
    std::optional<std::span<std::filesystem::path const>> list,
    std::shared_ptr<file_access const> fa,
    std::function<void(library_dependencies&)> const& extra_deps) {
  auto& prog = wprog.get_internal();
  auto& fsw = fs_writer.get_internal();

  if (!options_.debug_filter_function) {
    LOG_INFO << "scanning " << path;
  }

  prog.set_status_function(status_string);

  inode_manager im(LOG_GET_LOGGER, prog, path, options_.inode);
  file_scanner fs(LOG_GET_LOGGER, wg_, os_, im, prog,
                  {.hash_algo = options_.file_hash_algorithm,
                   .debug_inode_create = os_.getenv(kEnvVarDumpFilesRaw) ||
                                         os_.getenv(kEnvVarDumpFilesFinal)});

  auto root =
      list ? scan_list(path, *list, prog, fs) : scan_tree(path, prog, fs);

  if (options_.debug_filter_function) {
    return;
  }

  if (options_.remove_empty_dirs) {
    LOG_INFO << "removing empty directories...";
    auto d = dynamic_cast<dir*>(root.get());
    d->remove_empty_dirs(prog);
  }

  LOG_INFO << "assigning directory and link inodes...";

  uint32_t first_link_inode = 0;
  dir_set_inode_visitor dsiv(first_link_inode);
  root->accept(dsiv, true);

  uint32_t first_file_inode = first_link_inode;
  link_set_inode_visitor lsiv(first_file_inode);
  root->accept(lsiv, true);

  LOG_INFO << "waiting for background scanners...";

  wg_.wait();

  LOG_INFO << "scanning CPU time: "
           << time_with_unit(wg_.try_get_cpu_time().value_or(0ns));

  dump_state(kEnvVarDumpFilesRaw, "raw files", fa,
             [&fs](auto& os) { fs.dump(os); });

  LOG_INFO << "finalizing file inodes...";
  uint32_t first_device_inode = first_file_inode;
  fs.finalize(first_device_inode);

  dump_state(kEnvVarDumpFilesFinal, "final files", fa,
             [&fs](auto& os) { fs.dump(os); });

  // this must be done after finalizing the inodes since this is when
  // the file vectors are populated
  if (im.has_invalid_inodes()) {
    LOG_INFO << "trying to recover any invalid inodes...";
    im.try_scan_invalid(wg_, os_);
    wg_.wait();
  }

  LOG_INFO << "saved " << size_with_unit(prog.saved_by_deduplication) << " / "
           << size_with_unit(prog.original_size) << " in "
           << prog.duplicate_files << "/" << prog.files_found
           << " duplicate files";

  auto frag_info = im.fragment_category_info();

  if (auto catmgr = options_.inode.categorizer_mgr) {
    for (auto const& ci : frag_info.info) {
      LOG_VERBOSE << "found " << ci.fragment_count << " "
                  << catmgr->category_name(ci.category) << " fragments ("
                  << size_with_unit(ci.total_size) << ")";
    }
  }

  global_entry_data ge_data(options_.metadata);
  metadata_builder mdb(LOG_GET_LOGGER, options_.metadata);

  LOG_INFO << "assigning device inodes...";
  uint32_t first_pipe_inode = first_device_inode;
  device_set_inode_visitor devsiv(first_pipe_inode);
  root->accept(devsiv);
  mdb.set_devices(std::move(devsiv.device_ids()));

  LOG_INFO << "assigning pipe/socket inodes...";
  uint32_t last_inode = first_pipe_inode;
  pipe_set_inode_visitor pipsiv(last_inode);
  root->accept(pipsiv);

  LOG_INFO << "building metadata...";

  mdb.set_symlink_table_size(first_file_inode - first_link_inode);

  wg_.add_job([&] {
    LOG_INFO << "saving names and symlinks...";
    names_and_symlinks_visitor nlv(ge_data);
    root->accept(nlv);

    ge_data.index();

    LOG_INFO << "updating name and link indices...";
    root->walk([&](entry* ep) {
      ep->update(ge_data);
      if (auto* lp = dynamic_cast<link*>(ep)) {
        mdb.add_symlink_table_entry(
            ep->inode_num().value() - first_link_inode,
            ge_data.get_symlink_table_entry(lp->linkname()));
      }
    });
  });

  dump_state(kEnvVarDumpInodes, "inodes", fa, [&im](auto& os) { im.dump(os); });

  LOG_INFO << "building blocks...";

  // TODO:
  // - get rid of multiple worker groups
  // - instead, introduce "batches" to which work can be added, and
  //   which gets run on a worker groups; each batch keeps track of
  //   its CPU time and affects thread naming

  auto blockmgr = std::make_shared<block_manager>();

  {
    size_t const num_threads = options_.num_segmenter_workers;
    worker_group wg_ordering(LOG_GET_LOGGER, os_, "ordering", num_threads);
    worker_group wg_blockify(LOG_GET_LOGGER, os_, "blockify", num_threads);

    fsw.configure(frag_info.categories, num_threads);

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

      LOG_DEBUG << category_prefix(catmgr, category)
                << "segmenter will use up to "
                << size_with_unit(
                       segmenter_factory_.estimate_memory_usage(category, cc));

      wg_blockify.add_job([this, catmgr, blockmgr, category, cat_size, meta, cc,
                           &prog, &fsw, &im, &wg_ordering] {
        auto span = im.ordered_span(category, wg_ordering);
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

        for (auto const& ino : span) {
          prog.current.store(ino.get());

          // TODO: factor this code out
          auto f = ino->any();

          if (auto size = f->size(); size > 0 && !f->is_invalid()) {
            auto [mm, _, errors] = ino->mmap_any(os_);

            if (mm) {
              file_off_t offset{0};

              for (auto& frag : ino->fragments()) {
                if (frag.category() == category) {
                  fragment_chunkable fc(*ino, frag, offset, mm, catmgr);
                  seg.add_chunkable(fc);
                  prog.fragments_written++;
                }

                offset += frag.size();
              }
            } else {
              for (auto& [fp, e] : errors) {
                LOG_ERROR << "failed to map file " << fp->path_as_string()
                          << ": " << exception_str(e)
                          << ", creating empty inode";
                ++prog.errors;
              }
              for (auto& frag : ino->fragments()) {
                if (frag.category() == category) {
                  prog.fragments_found--;
                }
              }
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

  // seg.finish();
  wg_.wait();

  prog.set_status_function([](progress const&, size_t) {
    return "waiting for block compression to finish";
  });
  prog.current.store(nullptr);

  mdb.set_block_size(segmenter_factory_.get_block_size());

  LOG_INFO << "saving chunks...";
  mdb.gather_chunks(im, *blockmgr, prog.chunk_count);

  LOG_INFO << "saving directories...";
  save_directories_visitor sdv(first_link_inode);
  root->accept(sdv);
  mdb.gather_entries(sdv.get_directories(), ge_data, last_inode);

  LOG_INFO << "saving shared files table...";
  save_shared_files_visitor ssfv(first_file_inode, first_device_inode,
                                 fs.num_unique());
  root->accept(ssfv);
  mdb.set_shared_files_table(std::move(ssfv.get_shared_files()));

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

    std::transform(written_categories.begin(), written_categories.end(),
                   block_categories.begin(), [&](auto const& cat) {
                     return category_indices.at(cat.value());
                   });

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

  mdb.set_total_fs_size(prog.original_size);
  mdb.set_total_hardlink_size(prog.hardlink_size);
  mdb.gather_global_entry_data(ge_data);

  auto [schema, data] = metadata_freezer(LOG_GET_LOGGER).freeze(mdb.build());

  LOG_VERBOSE << "uncompressed metadata size: " << size_with_unit(data.size());

  fsw.write_metadata_v2_schema(schema);
  fsw.write_metadata_v2(data);

  if (options_.enable_history) {
    history hist(options_.history);
    hist.append(options_.command_line_arguments, extra_deps);
    fsw.write_history(hist.serialize());
  }

  LOG_INFO << "waiting for compression to finish...";

  fsw.flush();

  LOG_INFO << "compressed " << size_with_unit(prog.original_size) << " to "
           << size_with_unit(prog.compressed_size) << " (ratio="
           << static_cast<double>(prog.compressed_size) / prog.original_size
           << ")";
}

} // namespace internal

scanner::scanner(logger& lgr, thread_pool& pool, segmenter_factory& sf,
                 entry_factory& ef, os_access const& os,
                 scanner_options const& options)
    : impl_(
          make_unique_logging_object<impl, internal::scanner_, logger_policies>(
              lgr, pool.get_worker_group(), sf, ef, os, options)) {}

} // namespace dwarfs::writer
