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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <folly/ExceptionString.h>
#include <folly/system/HardwareConcurrency.h>

#include <fmt/format.h>

#include "dwarfs/block_data.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/categorizer.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/features.h"
#include "dwarfs/file_access.h"
#include "dwarfs/file_scanner.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fragment_chunkable.h"
#include "dwarfs/global_entry_data.h"
#include "dwarfs/history.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/inode_ordering.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/mmif.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "dwarfs/segmenter_factory.h"
#include "dwarfs/string_table.h"
#include "dwarfs/util.h"
#include "dwarfs/version.h"
#include "dwarfs/worker_group.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

using namespace std::chrono_literals;

namespace {

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

  void pack(thrift::metadata::metadata& mv2, global_entry_data& ge_data) {
    for (auto p : directories_) {
      if (!p->has_parent()) {
        p->set_entry_index(mv2.dir_entries()->size());
        p->pack_entry(mv2, ge_data);
      }

      p->pack(mv2, ge_data);
    }

    thrift::metadata::directory dummy;
    dummy.parent_entry() = 0;
    dummy.first_entry() = mv2.dir_entries()->size();
    mv2.directories()->push_back(dummy);

    directories_.clear();
  }

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

  void pack_shared_files() {
    if (!shared_files_.empty()) {
      DWARFS_CHECK(std::is_sorted(shared_files_.begin(), shared_files_.end()),
                   "shared files vector not sorted");
      std::vector<uint32_t> compressed;
      compressed.reserve(shared_files_.back() + 1);

      uint32_t count = 0;
      uint32_t index = 0;
      for (auto i : shared_files_) {
        if (i == index) {
          ++count;
        } else {
          ++index;
          DWARFS_CHECK(i == index, "inconsistent shared files vector");
          DWARFS_CHECK(count >= 2, "unique file in shared files vector");
          compressed.emplace_back(count - 2);
          count = 1;
        }
      }

      compressed.emplace_back(count - 2);

      DWARFS_CHECK(compressed.size() == shared_files_.back() + 1,
                   "unexpected compressed vector size");

      shared_files_.swap(compressed);
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
  scanner_(logger& lgr, worker_group& wg, std::shared_ptr<segmenter_factory> sf,
           std::shared_ptr<entry_factory> ef,
           std::shared_ptr<os_access const> os, std::shared_ptr<script> scr,
           const scanner_options& options);

  void scan(filesystem_writer& fsw, std::filesystem::path const& path,
            progress& prog,
            std::optional<std::span<std::filesystem::path const>> list,
            std::shared_ptr<file_access const> fa) override;

 private:
  std::shared_ptr<entry> scan_tree(std::filesystem::path const& path,
                                   progress& prog, detail::file_scanner& fs);

  std::shared_ptr<entry> scan_list(std::filesystem::path const& path,
                                   std::span<std::filesystem::path const> list,
                                   progress& prog, detail::file_scanner& fs);

  std::shared_ptr<entry>
  add_entry(std::filesystem::path const& name, std::shared_ptr<dir> parent,
            progress& prog, detail::file_scanner& fs,
            bool debug_filter = false);

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  scanner_options const& options_;
  std::shared_ptr<segmenter_factory> segmenter_factory_;
  std::shared_ptr<entry_factory> entry_factory_;
  std::shared_ptr<os_access const> os_;
  std::shared_ptr<script> script_;
};

template <typename LoggerPolicy>
scanner_<LoggerPolicy>::scanner_(logger& lgr, worker_group& wg,
                                 std::shared_ptr<segmenter_factory> sf,
                                 std::shared_ptr<entry_factory> ef,
                                 std::shared_ptr<os_access const> os,
                                 std::shared_ptr<script> scr,
                                 const scanner_options& options)
    : LOG_PROXY_INIT(lgr)
    , wg_{wg}
    , options_{options}
    , segmenter_factory_{std::move(sf)}
    , entry_factory_{std::move(ef)}
    , os_(std::move(os))
    , script_(std::move(scr)) {}

template <typename LoggerPolicy>
std::shared_ptr<entry>
scanner_<LoggerPolicy>::add_entry(std::filesystem::path const& name,
                                  std::shared_ptr<dir> parent, progress& prog,
                                  detail::file_scanner& fs, bool debug_filter) {
  try {
    auto pe = entry_factory_->create(*os_, name, parent);
    bool exclude = false;

    if (script_) {
      if (script_->has_filter() && !script_->filter(*pe)) {
        exclude = true;
      } else if (script_->has_transform()) {
        script_->transform(*pe);
      }
    }

    if (debug_filter) {
      (*options_.debug_filter_function)(exclude, pe.get());
    }

    if (exclude) {
      if (!debug_filter) {
        LOG_DEBUG << "excluding " << pe->dpath();
      }

      return nullptr;
    }

    if (pe) {
      switch (pe->type()) {
      case entry::E_FILE:
        if (pe->size() > 0 && os_->access(pe->fs_path(), R_OK)) {
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
          pe->scan(*os_, prog);
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
          pe->scan(*os_, prog);
        }
        prog.symlinks_scanned++;
        break;

      case entry::E_DEVICE:
      case entry::E_OTHER:
        prog.specials_found++;
        if (!debug_filter) {
          pe->scan(*os_, prog);
        }
        break;

      default:
        LOG_ERROR << "unsupported entry type: " << int(pe->type());
        prog.errors++;
        break;
      }
    }

    return pe;
  } catch (const std::system_error& e) {
    LOG_ERROR << "error reading entry: " << folly::exceptionStr(e);
    prog.errors++;
  }

  return nullptr;
}

template <typename LoggerPolicy>
std::shared_ptr<entry>
scanner_<LoggerPolicy>::scan_tree(std::filesystem::path const& path,
                                  progress& prog, detail::file_scanner& fs) {
  auto root = entry_factory_->create(*os_, path);
  bool const debug_filter = options_.debug_filter_function.has_value();

  if (root->type() != entry::E_DIR) {
    DWARFS_THROW(runtime_error,
                 fmt::format("'{}' must be a directory", path.string()));
  }

  if (script_ && script_->has_transform()) {
    script_->transform(*root);
  }

  std::deque<std::shared_ptr<entry>> queue({root});
  prog.dirs_found++;

  while (!queue.empty()) {
    auto parent = std::dynamic_pointer_cast<dir>(queue.front());

    DWARFS_CHECK(parent, "expected directory");

    queue.pop_front();
    auto path = parent->fs_path();

    try {
      auto d = os_->opendir(path);
      std::filesystem::path name;
      std::vector<std::shared_ptr<entry>> subdirs;

      while (d->read(name)) {
        if (auto pe = add_entry(name, parent, prog, fs, debug_filter)) {
          if (pe->type() == entry::E_DIR) {
            subdirs.push_back(pe);
          }
        }
      }

      queue.insert(queue.begin(), subdirs.begin(), subdirs.end());

      prog.dirs_scanned++;
    } catch (const std::system_error& e) {
      LOG_ERROR << "cannot read directory `" << path
                << "`: " << folly::exceptionStr(e);
      prog.errors++;
    }
  }

  return root;
}

template <typename LoggerPolicy>
std::shared_ptr<entry>
scanner_<LoggerPolicy>::scan_list(std::filesystem::path const& path,
                                  std::span<std::filesystem::path const> list,
                                  progress& prog, detail::file_scanner& fs) {
  if (script_ && script_->has_filter()) {
    DWARFS_THROW(runtime_error, "cannot use filters with file lists");
  }

  auto ti = LOG_TIMED_INFO;

  auto root = entry_factory_->create(*os_, path);

  if (root->type() != entry::E_DIR) {
    DWARFS_THROW(runtime_error,
                 fmt::format("'{}' must be a directory", path.string()));
  }

  if (script_ && script_->has_transform()) {
    script_->transform(*root);
  }

  auto ensure_path = [this, &prog, &fs](std::filesystem::path const& path,
                                        std::shared_ptr<entry> root) {
    for (auto const& p : path) {
      if (auto d = std::dynamic_pointer_cast<dir>(root)) {
        if (auto e = d->find(p.string())) {
          root = e;
        } else {
          root = add_entry(p, d, prog, fs);
          if (root && root->type() == entry::E_DIR) {
            prog.dirs_scanned++;
          } else {
            DWARFS_THROW(runtime_error,
                         fmt::format("invalid path '{}'", path.string()));
          }
        }
      } else {
        DWARFS_THROW(runtime_error,
                     fmt::format("invalid path '{}'", path.string()));
      }
    }

    return root;
  };

  std::unordered_map<std::string, std::shared_ptr<dir>> dir_cache;

  for (auto const& p : list) {
    auto pp = p.parent_path();
    std::shared_ptr<dir> pd;

    if (auto it = dir_cache.find(pp.string()); it != dir_cache.end()) {
      pd = it->second;
    } else {
      pd = std::dynamic_pointer_cast<dir>(ensure_path(pp, root));

      if (pd) {
        dir_cache.emplace(pp.string(), pd);
      } else {
        DWARFS_THROW(runtime_error,
                     fmt::format("invalid path '{}'", p.string()));
      }
    }

    if (auto pe = pd->find(p)) {
      continue;
    }

    if (auto pe = add_entry(p, pd, prog, fs)) {
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
    filesystem_writer& fsw, const std::filesystem::path& path, progress& prog,
    std::optional<std::span<std::filesystem::path const>> list,
    std::shared_ptr<file_access const> fa) {
  if (!options_.debug_filter_function) {
    LOG_INFO << "scanning " << path;
  }

  prog.set_status_function(status_string);

  inode_manager im(LOG_GET_LOGGER, prog, options_.inode);
  detail::file_scanner fs(LOG_GET_LOGGER, wg_, *os_, im,
                          options_.file_hash_algorithm, prog);

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
           << time_with_unit(wg_.get_cpu_time().value_or(0ns));

  LOG_INFO << "finalizing file inodes...";
  uint32_t first_device_inode = first_file_inode;
  fs.finalize(first_device_inode);

  // this must be done after finalizing the inodes since this is when
  // the file vectors are populated
  if (im.has_invalid_inodes()) {
    LOG_INFO << "trying to recover any invalid inodes...";
    im.try_scan_invalid(wg_, *os_);
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

  global_entry_data ge_data(options_);
  thrift::metadata::metadata mv2;
  feature_set features;

  mv2.symlink_table()->resize(first_file_inode - first_link_inode);

  LOG_INFO << "assigning device inodes...";
  uint32_t first_pipe_inode = first_device_inode;
  device_set_inode_visitor devsiv(first_pipe_inode);
  root->accept(devsiv);
  mv2.devices() = std::move(devsiv.device_ids());

  LOG_INFO << "assigning pipe/socket inodes...";
  uint32_t last_inode = first_pipe_inode;
  pipe_set_inode_visitor pipsiv(last_inode);
  root->accept(pipsiv);

  LOG_INFO << "building metadata...";

  wg_.add_job([&] {
    LOG_INFO << "saving names and symlinks...";
    names_and_symlinks_visitor nlv(ge_data);
    root->accept(nlv);

    ge_data.index();

    LOG_INFO << "updating name and link indices...";
    root->walk([&](entry* ep) {
      ep->update(ge_data);
      if (auto lp = dynamic_cast<link*>(ep)) {
        DWARFS_NOTHROW(mv2.symlink_table()->at(ep->inode_num().value() -
                                               first_link_inode)) =
            ge_data.get_symlink_table_entry(lp->linkname());
      }
    });
  });

  if (auto dumpfile = os_->getenv("DWARFS_DUMP_INODES")) {
    if (fa) {
      LOG_VERBOSE << "dumping inodes to " << *dumpfile;
      std::error_code ec;
      auto ofs = fa->open_output(*dumpfile, ec);
      if (ec) {
        LOG_ERROR << "cannot open '" << *dumpfile << "': " << ec.message();
      } else {
        im.dump(ofs->os());
        ofs->close(ec);
        if (ec) {
          LOG_ERROR << "cannot close '" << *dumpfile << "': " << ec.message();
        }
      }
    } else {
      LOG_ERROR << "cannot dump inodes: no file access";
    }
  }

  LOG_INFO << "building blocks...";

  // TODO:
  // - get rid of multiple worker groups
  // - instead, introduce "batches" to which work can be added, and
  //   which gets run on a worker groups; each batch keeps track of
  //   its CPU time and affects thread naming

  auto blockmgr = std::make_shared<block_manager>();

  {
    size_t const num_threads = options_.num_segmenter_workers;
    worker_group wg_ordering(LOG_GET_LOGGER, *os_, "ordering", num_threads);
    worker_group wg_blockify(LOG_GET_LOGGER, *os_, "blockify", num_threads);

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

      wg_blockify.add_job([this, catmgr, blockmgr, category, cat_size, meta, cc,
                           &prog, &fsw, &im, &wg_ordering] {
        auto span = im.ordered_span(category, wg_ordering);
        auto tv = LOG_CPU_TIMED_VERBOSE;

        auto seg = segmenter_factory_->create(
            category, cat_size, cc, blockmgr,
            [category, meta, blockmgr, &fsw](auto block,
                                             auto logical_block_num) {
              fsw.write_block(
                  category, std::move(block),
                  [blockmgr, logical_block_num,
                   category](auto physical_block_num) {
                    blockmgr->set_written_block(logical_block_num,
                                                physical_block_num,
                                                category.value());
                  },
                  meta);
            });

        for (auto ino : span) {
          prog.current.store(ino.get());

          // TODO: factor this code out
          auto f = ino->any();

          if (auto size = f->size(); size > 0 && !f->is_invalid()) {
            auto [mm, _, errors] = ino->mmap_any(*os_);

            if (mm) {
              file_off_t offset{0};

              for (auto& frag : ino->fragments()) {
                if (frag.category() == category) {
                  fragment_chunkable fc(*ino, frag, offset, *mm, catmgr);
                  seg.add_chunkable(fc);
                  prog.fragments_written++;
                }

                offset += frag.size();
              }
            } else {
              for (auto& [fp, e] : errors) {
                LOG_ERROR << "failed to map file " << fp->path_as_string()
                          << ": " << folly::exceptionStr(e)
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
             << time_with_unit(wg_ordering.get_cpu_time().value_or(0ns));

    LOG_INFO << "total segmenting CPU time: "
             << time_with_unit(wg_blockify.get_cpu_time().value_or(0ns));
  }

  // seg.finish();
  wg_.wait();

  prog.set_status_function([](progress const&, size_t) {
    return "waiting for block compression to finish";
  });
  prog.current.store(nullptr);

  // this is actually needed
  root->set_name(std::string());

  LOG_INFO << "saving chunks...";
  mv2.chunk_table()->resize(im.count() + 1);

  // TODO: we should be able to start this once all blocks have been
  //       submitted for compression
  mv2.chunks().value().reserve(prog.chunk_count);
  im.for_each_inode_in_order([&](std::shared_ptr<inode> const& ino) {
    DWARFS_NOTHROW(mv2.chunk_table()->at(ino->num())) = mv2.chunks()->size();
    if (!ino->append_chunks_to(mv2.chunks().value())) {
      std::ostringstream oss;
      for (auto fp : ino->all()) {
        oss << "\n  " << fp->path_as_string();
      }
      LOG_ERROR << "inconsistent fragments in inode " << ino->num()
                << ", the following files will be empty:" << oss.str();
    }
  });

  blockmgr->map_logical_blocks(mv2.chunks().value());

  // insert dummy inode to help determine number of chunks per inode
  DWARFS_NOTHROW(mv2.chunk_table()->at(im.count())) = mv2.chunks()->size();

  LOG_DEBUG << "total number of unique files: " << im.count();
  LOG_DEBUG << "total number of chunks: " << mv2.chunks()->size();

  LOG_INFO << "saving directories...";
  mv2.dir_entries() = std::vector<thrift::metadata::dir_entry>();
  mv2.inodes()->resize(last_inode);
  mv2.directories()->reserve(first_link_inode + 1);
  save_directories_visitor sdv(first_link_inode);
  root->accept(sdv);
  sdv.pack(mv2, ge_data);

  if (options_.pack_directories) {
    // pack directories
    uint32_t last_first_entry = 0;

    for (auto& d : mv2.directories().value()) {
      d.parent_entry() = 0; // this will be recovered
      auto delta = d.first_entry().value() - last_first_entry;
      last_first_entry = d.first_entry().value();
      d.first_entry() = delta;
    }
  }

  if (options_.pack_chunk_table) {
    // delta-compress chunk table
    std::adjacent_difference(mv2.chunk_table()->begin(),
                             mv2.chunk_table()->end(),
                             mv2.chunk_table()->begin());
  }

  LOG_INFO << "saving shared files table...";
  save_shared_files_visitor ssfv(first_file_inode, first_device_inode,
                                 fs.num_unique());
  root->accept(ssfv);
  if (options_.pack_shared_files_table) {
    ssfv.pack_shared_files();
  }
  mv2.shared_files_table() = std::move(ssfv.get_shared_files());

  thrift::metadata::fs_options fsopts;
  fsopts.mtime_only() = !options_.keep_all_times;
  if (options_.time_resolution_sec > 1) {
    fsopts.time_resolution_sec() = options_.time_resolution_sec;
  }
  fsopts.packed_chunk_table() = options_.pack_chunk_table;
  fsopts.packed_directories() = options_.pack_directories;
  fsopts.packed_shared_files_table() = options_.pack_shared_files_table;

  if (options_.plain_names_table) {
    mv2.names() = ge_data.get_names();
  } else {
    auto ti = LOG_TIMED_INFO;
    mv2.compact_names() = string_table::pack(
        ge_data.get_names(), string_table::pack_options(
                                 options_.pack_names, options_.pack_names_index,
                                 options_.force_pack_string_tables));
    ti << "saving names table...";
  }

  if (options_.plain_symlinks_table) {
    mv2.symlinks() = ge_data.get_symlinks();
  } else {
    auto ti = LOG_TIMED_INFO;
    mv2.compact_symlinks() = string_table::pack(
        ge_data.get_symlinks(),
        string_table::pack_options(options_.pack_symlinks,
                                   options_.pack_symlinks_index,
                                   options_.force_pack_string_tables));
    ti << "saving symlinks table...";
  }

  mv2.uids() = ge_data.get_uids();
  mv2.gids() = ge_data.get_gids();
  mv2.modes() = ge_data.get_modes();
  mv2.timestamp_base() = ge_data.get_timestamp_base();
  mv2.block_size() = segmenter_factory_->get_block_size();
  mv2.total_fs_size() = prog.original_size;
  mv2.total_hardlink_size() = prog.hardlink_size;
  mv2.options() = fsopts;
  mv2.dwarfs_version() = std::string("libdwarfs ") + PRJ_GIT_ID;
  if (!options_.no_create_timestamp) {
    mv2.create_timestamp() = std::time(nullptr);
  }
  mv2.preferred_path_separator() =
      static_cast<uint32_t>(std::filesystem::path::preferred_separator);

  if (auto catmgr = options_.inode.categorizer_mgr) {
    std::unordered_map<fragment_category::value_type,
                       fragment_category::value_type>
        category_indices;
    std::vector<std::string> category_names;

    category_indices.reserve(frag_info.info.size());
    category_names.reserve(frag_info.info.size());

    for (auto const& ci : frag_info.info) {
      auto [it, inserted] =
          category_indices.emplace(ci.category, category_names.size());
      if (inserted) {
        category_names.emplace_back(catmgr->category_name(ci.category));
      }
    }

    auto written_categories = blockmgr->get_written_block_categories();

    std::transform(written_categories.begin(), written_categories.end(),
                   written_categories.begin(),
                   [&](auto const& cat) { return category_indices.at(cat); });

    mv2.category_names() = std::move(category_names);
    mv2.block_categories() = std::move(written_categories);
  }

  mv2.features() = features.get();

  auto [schema, data] = metadata_v2::freeze(mv2);

  LOG_VERBOSE << "uncompressed metadata size: " << size_with_unit(data.size());

  fsw.write_metadata_v2_schema(std::make_shared<block_data>(std::move(schema)));
  fsw.write_metadata_v2(std::make_shared<block_data>(std::move(data)));

  if (options_.enable_history) {
    history hist(options_.history);
    hist.append(options_.command_line_arguments);
    fsw.write_history(std::make_shared<block_data>(hist.serialize()));
  }

  LOG_INFO << "waiting for compression to finish...";

  fsw.flush();

  LOG_INFO << "compressed " << size_with_unit(prog.original_size) << " to "
           << size_with_unit(prog.compressed_size) << " (ratio="
           << static_cast<double>(prog.compressed_size) / prog.original_size
           << ")";
}

scanner::scanner(logger& lgr, worker_group& wg,
                 std::shared_ptr<segmenter_factory> sf,
                 std::shared_ptr<entry_factory> ef,
                 std::shared_ptr<os_access const> os,
                 std::shared_ptr<script> scr, const scanner_options& options)
    : impl_(make_unique_logging_object<impl, scanner_, logger_policies>(
          lgr, wg, std::move(sf), std::move(ef), std::move(os), std::move(scr),
          options)) {}

} // namespace dwarfs
