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
#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

#include <boost/system/system_error.hpp>

#include <folly/ExceptionString.h>

#include <fmt/format.h>

#include "dwarfs/entry.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/global_entry_data.h"
#include "dwarfs/hash_util.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata_v2.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

namespace {

class visitor_base : public entry_visitor {
 public:
  void visit(file*) override {}
  void visit(link*) override {}
  void visit(dir*) override {}
  void visit(device*) override {}
};

class scan_files_visitor : public visitor_base {
 public:
  scan_files_visitor(worker_group& wg, os_access& os, progress& prog)
      : wg_(wg)
      , os_(os)
      , prog_(prog) {}

  // TODO: avoid scanning hardlinks multiple times
  void visit(file* p) override {
    wg_.add_job([=] {
      prog_.current.store(p);
      p->scan(os_, prog_);
      prog_.files_scanned++;
    });
  }

 private:
  worker_group& wg_;
  os_access& os_;
  progress& prog_;
};

class file_deduplication_visitor : public visitor_base {
 public:
  void visit(file* p) override { hash_[p->hash()].push_back(p); }

  void deduplicate_files(worker_group& wg, os_access& os, inode_manager& im,
                         inode_options const& ino_opts, progress& prog) {
    for (auto& p : hash_) {
      auto& files = p.second;

      if (files.size() > 1) {
        std::sort(files.begin(), files.end(), [](file const* a, file const* b) {
          return a->path() < b->path();
        });
      }

      auto inode = im.create_inode();

      for (auto fp : files) {
        fp->set_inode(inode);
      }

      if (auto dupes = files.size() - 1; dupes > 0) {
        prog.duplicate_files += dupes;
        prog.saved_by_deduplication += dupes * files.front()->size();
      }

      inode->set_files(std::move(files));

      if (ino_opts.needs_scan()) {
        wg.add_job([&, inode] { inode->scan(os, ino_opts); });
      }
    }
  }

 private:
  std::unordered_map<std::string_view, inode::files_vector, folly::Hash> hash_;
};

class dir_set_inode_visitor : public visitor_base {
 public:
  dir_set_inode_visitor(uint32_t& inode_no)
      : inode_no_(inode_no) {}

  void visit(dir* p) override {
    p->sort();
    p->set_inode(inode_no_++);
  }

  uint32_t inode_no() const { return inode_no_; }

 private:
  uint32_t& inode_no_;
};

class link_set_inode_visitor : public visitor_base {
 public:
  link_set_inode_visitor(uint32_t& inode_no)
      : inode_no_(inode_no) {}

  void visit(link* p) override { p->set_inode(inode_no_++); }

 private:
  uint32_t& inode_no_;
};

class device_set_inode_visitor : public visitor_base {
 public:
  device_set_inode_visitor(uint32_t& inode_no)
      : inode_no_(inode_no) {}

  void visit(device* p) override {
    if (p->type() == entry::E_DEVICE) {
      p->set_inode(inode_no_++);
      dev_ids_.push_back(p->device_id());
    }
  }

  std::vector<uint64_t>& device_ids() { return dev_ids_; }

 private:
  std::vector<uint64_t> dev_ids_;
  uint32_t& inode_no_;
};

class pipe_set_inode_visitor : public visitor_base {
 public:
  pipe_set_inode_visitor(uint32_t& inode_no)
      : inode_no_(inode_no) {}

  void visit(device* p) override {
    if (p->type() != entry::E_DEVICE) {
      p->set_inode(inode_no_++);
    }
  }

 private:
  uint32_t& inode_no_;
};

class names_and_links_visitor : public entry_visitor {
 public:
  names_and_links_visitor(global_entry_data& data)
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
  save_directories_visitor(size_t num_directories) {
    directories_.resize(num_directories);
  }

  void visit(dir* p) override { directories_[p->inode_num()] = p; }

  void pack(thrift::metadata::metadata& mv2, global_entry_data& ge_data) {
    for (auto p : directories_) {
      if (!p->has_parent()) {
        p->pack_entry(mv2, ge_data);
      }

      p->pack(mv2, ge_data);
    }

    thrift::metadata::directory dummy;
    dummy.parent_inode = 0;
    dummy.first_entry = mv2.entries.size();
    mv2.directories.push_back(dummy);
  }

 private:
  std::vector<dir*> directories_;
};

std::string status_string(progress const& p, size_t width) {
  auto cp = p.current.load();
  std::string label, path;

  if (cp) {
    if (auto e = dynamic_cast<entry_interface const*>(cp)) {
      label = "scanning: ";
      path = e->path();
    } else if (auto i = dynamic_cast<inode const*>(cp)) {
      label = "writing: ";
      path = i->any()->path();
    }
    auto max_len = width - label.size();
    auto len = path.size();
    if (len > max_len) {
      // TODO: get this correct for UTF8 multibyte chars :-)
      size_t start = 0;
      max_len -= 1;
      while (start != std::string::npos && (len - start) > max_len) {
        start = path.find('/', start + 1);
      }
      if (start == std::string::npos) {
        start = max_len - len;
      }
      path.replace(0, start, "…");
    }
  }

  return label + path;
}

} // namespace

template <typename LoggerPolicy>
class scanner_ : public scanner::impl {
 public:
  scanner_(logger& lgr, worker_group& wg, const block_manager::config& config,
           std::shared_ptr<entry_factory> ef, std::shared_ptr<os_access> os,
           std::shared_ptr<script> scr, const scanner_options& options);

  void scan(filesystem_writer& fsw, const std::string& path, progress& prog);

 private:
  std::shared_ptr<entry> scan_tree(const std::string& path, progress& prog);

  const block_manager::config& cfg_;
  const scanner_options& options_;
  std::shared_ptr<entry_factory> entry_;
  std::shared_ptr<os_access> os_;
  std::shared_ptr<script> script_;
  worker_group& wg_;
  logger& lgr_;
  log_proxy<LoggerPolicy> log_;
};

template <typename LoggerPolicy>
scanner_<LoggerPolicy>::scanner_(logger& lgr, worker_group& wg,
                                 const block_manager::config& cfg,
                                 std::shared_ptr<entry_factory> ef,
                                 std::shared_ptr<os_access> os,
                                 std::shared_ptr<script> scr,
                                 const scanner_options& options)
    : cfg_(cfg)
    , options_(options)
    , entry_(std::move(ef))
    , os_(std::move(os))
    , script_(std::move(scr))
    , wg_(wg)
    , lgr_(lgr)
    , log_(lgr) {}

template <typename LoggerPolicy>
std::shared_ptr<entry>
scanner_<LoggerPolicy>::scan_tree(const std::string& path, progress& prog) {
  auto root = entry_->create(*os_, path);

  if (root->type() != entry::E_DIR) {
    throw std::runtime_error(path + " must be a directory");
  }

  std::deque<std::shared_ptr<entry>> queue({root});
  prog.dirs_found++;

  while (!queue.empty()) {
    auto parent = std::dynamic_pointer_cast<dir>(queue.front());

    if (!parent) {
      throw std::runtime_error("expected directory");
    }

    queue.pop_front();
    const std::string& path = parent->path();

    try {
      auto d = os_->opendir(path);
      std::string name;
      std::vector<std::shared_ptr<entry>> subdirs;

      while (d->read(name)) {
        if (name == "." or name == "..") {
          continue;
        }

        try {
          auto pe = entry_->create(*os_, name, parent);

          if (script_) {
            if (script_->has_filter() && !script_->filter(*pe)) {
              log_.debug() << "skipping " << name;
              continue;
            }

            if (script_->has_transform()) {
              script_->transform(*pe);
            }
          }

          if (pe) {
            if (pe->type() == entry::E_FILE && os_->access(pe->path(), R_OK)) {
              log_.error() << "cannot access: " << pe->path();
              prog.errors++;
              continue;
            }

            parent->add(pe);

            switch (pe->type()) {
            case entry::E_DIR:
              prog.current.store(pe.get());
              prog.dirs_found++;
              pe->scan(*os_, prog);
              subdirs.push_back(pe);
              break;

            case entry::E_FILE:
              prog.files_found++;
              break;

            case entry::E_LINK:
              prog.links_found++;
              pe->scan(*os_, prog);
              prog.links_scanned++;
              break;

            case entry::E_DEVICE:
            case entry::E_OTHER:
              prog.specials_found++;
              pe->scan(*os_, prog);
              break;

            default:
              log_.error() << "unsupported entry type: " << int(pe->type());
              prog.errors++;
              break;
            }
          }
        } catch (const boost::system::system_error& e) {
          log_.error() << "error reading entry: " << e.what();
          prog.errors++;
        }
      }

      queue.insert(queue.begin(), subdirs.begin(), subdirs.end());

      prog.dirs_scanned++;
    } catch (const boost::system::system_error& e) {
      log_.error() << "cannot open directory: " << e.what();
      prog.errors++;
    }
  }

  return root;
}

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan(filesystem_writer& fsw,
                                  const std::string& path, progress& prog) {
  log_.info() << "scanning " << path;

  prog.set_status_function(status_string);

  auto root = scan_tree(path, prog);

  if (options_.remove_empty_dirs) {
    log_.info() << "removing empty directories...";
    auto d = dynamic_cast<dir*>(root.get());
    d->remove_empty_dirs(prog);
  }

  // now scan all files
  scan_files_visitor sfv(wg_, *os_, prog);
  root->accept(sfv);

  log_.info() << "waiting for background scanners...";
  wg_.wait();

  log_.info() << "assigning directory and link inodes...";

  uint32_t first_link_inode = 0;
  dir_set_inode_visitor dsiv(first_link_inode);
  root->accept(dsiv, true);

  uint32_t first_file_inode = first_link_inode;
  link_set_inode_visitor lsiv(first_file_inode);
  root->accept(lsiv, true);

  log_.info() << "finding duplicate files...";

  inode_manager im(lgr_);

  file_deduplication_visitor fdv;
  root->accept(fdv);

  fdv.deduplicate_files(wg_, *os_, im, options_.inode, prog);

  log_.info() << "saved " << size_with_unit(prog.saved_by_deduplication)
              << " / " << size_with_unit(prog.original_size) << " in "
              << prog.duplicate_files << "/" << prog.files_found
              << " duplicate files";

  if (options_.inode.needs_scan()) {
    log_.info() << "waiting for inode scanners...";
    wg_.wait();
  }

  global_entry_data ge_data(options_);
  thrift::metadata::metadata mv2;

  mv2.link_index.resize(first_file_inode - first_link_inode);

  log_.info() << "assigning device inodes...";
  uint32_t first_device_inode = first_file_inode + im.count();
  device_set_inode_visitor devsiv(first_device_inode);
  root->accept(devsiv);
  mv2.devices_ref() = std::move(devsiv.device_ids());

  log_.info() << "assigning pipe/socket inodes...";
  uint32_t first_pipe_inode = first_device_inode;
  pipe_set_inode_visitor pipsiv(first_pipe_inode);
  root->accept(pipsiv);

  log_.info() << "building metadata...";

  wg_.add_job([&] {
    log_.info() << "saving names and links...";
    names_and_links_visitor nlv(ge_data);
    root->accept(nlv);

    ge_data.index();

    log_.info() << "updating name and link indices...";
    root->walk([&](entry* ep) {
      ep->update(ge_data);
      if (auto lp = dynamic_cast<link*>(ep)) {
        mv2.link_index.at(ep->inode_num() - first_link_inode) =
            ge_data.get_link_index(lp->linkname());
      }
    });
  });

  log_.info() << "building blocks...";
  block_manager bm(lgr_, prog, cfg_, os_, fsw);

  im.order_inodes(script_, options_.file_order, first_file_inode,
                  [&](std::shared_ptr<inode> const& ino) {
                    prog.current.store(ino.get());
                    bm.add_inode(ino);
                    prog.inodes_written++;
                  });

  log_.info() << "waiting for block compression to finish...";

  bm.finish_blocks();
  wg_.wait();

  prog.set_status_function([](progress const&, size_t) {
    return "waiting for block compression to finish";
  });
  prog.sync([&] { prog.current.store(nullptr); });

  // TODO: check this, doesn't seem to come out right in debug output
  //       seems to be out-of-line with block compression??
  log_.debug() << "compressed " << size_with_unit(bm.total_size()) << " in "
               << bm.total_blocks() << " blocks to "
               << size_with_unit(prog.compressed_size) << " (ratio="
               << (bm.total_size() ? static_cast<double>(prog.compressed_size) /
                                         bm.total_size()
                                   : 1.0)
               << ")";

  log_.debug() << "saved by segmenting: "
               << size_with_unit(prog.saved_by_segmentation);

  // this is actually needed
  root->set_name(std::string());

  log_.info() << "saving chunks...";
  mv2.chunk_index.resize(im.count() + 1);

  // TODO: we should be able to start this once all blocks have been
  //       submitted for compression
  im.for_each_inode([&](std::shared_ptr<inode> const& ino) {
    mv2.chunk_index.at(ino->num() - first_file_inode) = mv2.chunks.size();
    ino->append_chunks_to(mv2.chunks);
  });

  // insert dummy inode to help determine number of chunks per inode
  mv2.chunk_index.at(im.count()) = mv2.chunks.size();

  log_.debug() << "total number of file inodes: " << im.count();
  log_.debug() << "total number of chunks: " << mv2.chunks.size();

  log_.info() << "saving directories...";
  mv2.entry_index.resize(first_pipe_inode);
  mv2.directories.reserve(first_link_inode + 1);
  save_directories_visitor sdv(first_link_inode);
  root->accept(sdv);
  sdv.pack(mv2, ge_data);

  thrift::metadata::fs_options fsopts;
  fsopts.mtime_only = !options_.keep_all_times;
  if (options_.time_resolution_sec > 1) {
    fsopts.time_resolution_sec_ref() = options_.time_resolution_sec;
  }

  mv2.uids = ge_data.get_uids();
  mv2.gids = ge_data.get_gids();
  mv2.modes = ge_data.get_modes();
  mv2.names = ge_data.get_names();
  mv2.links = ge_data.get_links();
  mv2.timestamp_base = ge_data.get_timestamp_base();
  mv2.block_size = UINT32_C(1) << cfg_.block_size_bits;
  mv2.total_fs_size = prog.original_size;
  mv2.options_ref() = fsopts;

  auto [schema, data] = metadata_v2::freeze(mv2);

  fsw.write_metadata_v2_schema(std::move(schema));
  fsw.write_metadata_v2(std::move(data));

  log_.info() << "waiting for compression to finish...";

  fsw.flush();

  log_.info() << "compressed " << size_with_unit(prog.original_size) << " to "
              << size_with_unit(prog.compressed_size) << " (ratio="
              << static_cast<double>(prog.compressed_size) / prog.original_size
              << ")";
}

scanner::scanner(logger& lgr, worker_group& wg,
                 const block_manager::config& cfg,
                 std::shared_ptr<entry_factory> ef,
                 std::shared_ptr<os_access> os, std::shared_ptr<script> scr,
                 const scanner_options& options)
    : impl_(make_unique_logging_object<impl, scanner_, logger_policies>(
          lgr, wg, cfg, std::move(ef), std::move(os), std::move(scr),
          options)) {}

} // namespace dwarfs
