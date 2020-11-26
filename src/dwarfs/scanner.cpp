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

#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/system/system_error.hpp>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/small_vector.h>

#include <sparsehash/dense_hash_map>
#include <sparsehash/dense_hash_set>

#include "dwarfs/config.h"
#include "dwarfs/cyclic_hash.h"
#include "dwarfs/entry.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/hash_util.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/logger.h"
#include "dwarfs/metadata.h"
#include "dwarfs/metadata_writer.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/scanner.h"
#include "dwarfs/script.h"
#include "dwarfs/util.h"

#include "dwarfs/gen-cpp2/metadata_layouts.h"
#include "dwarfs/gen-cpp2/metadata_types.h"
#include "dwarfs/gen-cpp2/metadata_types_custom_protocol.h"
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs {

namespace {

template <class T>
std::vector<uint8_t> freeze_to_buffer(const T& x) {
  using namespace ::apache::thrift::frozen;

  Layout<T> layout;
  size_t content_size = LayoutRoot::layout(x, layout);

  std::string schema;
  serializeRootLayout(layout, schema);

  size_t schema_size = schema.size();
  auto schema_begin = reinterpret_cast<uint8_t const*>(schema.data());
  std::vector<uint8_t> buffer(schema_begin, schema_begin + schema_size);

  size_t buffer_size = schema_size + content_size;
  buffer.resize(buffer_size, 0);

  folly::MutableByteRange content_range(&buffer[schema_size], content_size);
  ByteRangeFreezer::freeze(layout, x, content_range);

  buffer.resize(buffer.size() - content_range.size());

  return buffer;
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
  template <typename Key, typename Value, typename HashKey = std::hash<Key>>
  class fast_hash_map : public google::dense_hash_map<Key, Value, HashKey> {
   public:
    fast_hash_map() { this->set_empty_key(Key()); }
  };

  template <typename T, typename HashT = std::hash<T>>
  class fast_hash_set : public google::dense_hash_set<T, HashT> {
   public:
    fast_hash_set() { this->set_empty_key(T()); }
  };

  // We want these to be ordered
  // TODO: StringPiece?
  // TODO: Use dense/unordered maps/sets and sort later?
  using file_name_table_t =
      fast_hash_map<size_t, fast_hash_set<std::string_view, folly::Hash>>;

  std::unordered_map<std::string_view, size_t, folly::Hash>
  compress_names_table(metadata_writer& mw,
                       const file_name_table_t& file_name) const;

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
std::unordered_map<std::string_view, size_t, folly::Hash>
scanner_<LoggerPolicy>::compress_names_table(
    metadata_writer& mw, const file_name_table_t& file_name) const {
  log_.info() << "compressing names table...";
  auto ti = log_.timed_info();

  google::dense_hash_map<uint32_t, uint32_t> index;
  using position_vector = folly::small_vector<uint32_t, 4>;
  std::vector<position_vector> positions;
  index.set_empty_key(0);
  uint32_t index_pos = 0;

  std::unordered_map<std::string_view, size_t, folly::Hash> offset;
  size_t saved = 0;
  size_t orig_offset = mw.offset();

  std::vector<size_t> sizes(file_name.size());
  std::transform(file_name.begin(), file_name.end(), sizes.begin(),
                 [](const auto& p) { return p.first; });
  std::sort(sizes.begin(), sizes.end(), std::greater<size_t>());

  for (auto size : sizes) {
    auto nsi = file_name.find(size);
    assert(nsi != file_name.end());
    std::vector<std::string_view> names(nsi->second.size());
    std::copy(nsi->second.begin(), nsi->second.end(), names.begin());
    std::sort(names.begin(), names.end());

    for (auto k : names) {
      bool found = false;

      if (!index.empty() && k.size() >= sizeof(uint32_t)) {
        uint32_t key;
        std::memcpy(&key, k.data(), sizeof(key));
        auto it = index.find(key);
        if (it != index.end()) {
          for (uint32_t pos : positions[it->second]) {
            if (std::memcmp(mw.section_data() + pos + sizeof(key),
                            k.data() + sizeof(key),
                            k.size() - sizeof(key)) == 0) {
              offset[k] = mw.section_data_offset() + pos;
              saved += k.size();
              found = true;
              break;
            }
          }
        }
      } else {
        auto it = std::search(mw.section_begin(), mw.end(), k.begin(), k.end());

        if (it != mw.end()) {
          offset[k] = it - mw.begin();
          saved += k.size();
          found = true;
        }
      }

      if (!found) {
        offset[k] = mw.offset();
        mw.write(k);

        if (mw.section_data_size() >= sizeof(uint32_t)) {
          uint32_t last = mw.section_data_size() - sizeof(uint32_t);
          while (index_pos <= last) {
            uint32_t key;
            std::memcpy(&key, mw.section_data() + index_pos, sizeof(key));
            auto r = index.insert(std::make_pair(key, positions.size()));
            uint32_t pos_index;
            if (r.second) {
              pos_index = positions.size();
              positions.resize(pos_index + 1);
            } else {
              pos_index = r.first->second;
            }
            positions[pos_index].push_back(index_pos++);
          }
        }
      }
    }
  }

  ti << "names table: " << size_with_unit(mw.offset() - orig_offset) << " ("
     << size_with_unit(saved) << " saved)";

  return offset;
}

class set_inode_visitor : public entry_visitor {
 public:
  void visit(file*) override {
    // nothing
  }

  void visit(link* p) override { p->set_inode(inode_no_++); }

  void visit(dir* p) override {
    p->sort();
    p->set_inode(inode_no_++);
  }

  uint32_t inode_no() const { return inode_no_; }

 private:
  uint32_t inode_no_ = 0;
};

class names_and_links_visitor : public entry_visitor {
 public:
  names_and_links_visitor(metadata_writer& mw, global_entry_data& data)
      : mw_(mw)
      , data_(data) {}

  void visit(file* p) override { data_.add_name(p->name()); }

  void visit(link* p) override {
    data_.add_name(p->name());
    data_.add_link(p->linkname());

    const auto& name = p->linkname();
    auto r = offset_.emplace(name, mw_.offset());
    if (r.second) {
      uint16_t len = folly::to<uint16_t>(name.size());
      mw_.write(len);
      mw_.write(name);
    }
    p->set_offset(r.first->second);
  }

  void visit(dir* p) override {
    if (p->has_parent()) {
      data_.add_name(p->name());
    }
  }

 private:
  metadata_writer& mw_;
  global_entry_data& data_;
  std::unordered_map<std::string_view, size_t, folly::Hash> offset_;
};

class save_directories_visitor : public entry_visitor {
 public:
  save_directories_visitor(metadata_writer& mw, thrift::metadata::metadata& mv2,
                           global_entry_data const& ge_data,
                           std::vector<uint32_t>& index)
      : mw_(mw)
      , mv2_(mv2)
      , ge_data_(ge_data)
      , cb_([&](const entry* e, size_t offset) {
        index.at(e->inode_num()) = folly::to<uint32_t>(offset);
      }) {}

  void visit(file*) override {
    // nothing
  }

  void visit(link*) override {
    // nothing
  }

  void visit(dir* p) override {
    mv2_.dir_link_index.at(p->inode_num()) = mv2_.directories.size();
    p->pack(mv2_, ge_data_);

    p->set_offset(mw_.offset());
    p->pack(mw_.buffer(p->packed_size()), cb_);

    if (!p->has_parent()) {
      cb_(p, mw_.offset());
      p->pack_entry(mw_.buffer(p->packed_entry_size()));
      p->pack_entry(mv2_, ge_data_);
    }
  }

 private:
  metadata_writer& mw_;
  thrift::metadata::metadata& mv2_;
  global_entry_data const& ge_data_;
  std::function<void(const entry* e, size_t offset)> cb_;
};

template <typename LoggerPolicy>
void scanner_<LoggerPolicy>::scan(filesystem_writer& fsw,
                                  const std::string& path, progress& prog) {
  log_.info() << "scanning " << path;

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

          if (script_ && !script_->filter(*pe)) {
            log_.debug() << "skipping " << name;
            continue;
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

            default:
              log_.error() << "unsupported entry type: " << int(pe->type());
              prog.errors++;
              break;
            }
          }
        } catch (const boost::system::system_error& e) {
          log_.error() << "error reading entry: " << e.what();
          prog.errors++;
        } catch (const std::exception& e) {
          log_.error() << folly::exceptionStr(e.what());
          prog.errors++;
        }
      }

      queue.insert(queue.begin(), subdirs.begin(), subdirs.end());

      prog.dirs_scanned++;
    } catch (const boost::system::system_error& e) {
      log_.error() << "cannot open directory: " << e.what();
      prog.errors++;
    } catch (const std::exception& e) {
      log_.error() << folly::exceptionStr(e.what());
      prog.errors++;
    }
  }

  // now scan all files
  root->walk([&](entry* ep) {
    wg_.add_job([=, &prog] {
      if (ep->type() == entry::E_FILE) {
        prog.current.store(ep);
        ep->scan(*os_, prog);
        prog.files_scanned++;
      }
    });
  });

  log_.info() << "waiting for background scanners...";
  wg_.wait();

  size_t total{0};
  std::unordered_map<std::string_view, std::vector<file*>, folly::Hash>
      file_hash;
  file_name_table_t file_name;

  root->walk([&](entry* ep) {
    if (auto fp = dynamic_cast<file*>(ep)) {
      file_hash[fp->hash()].push_back(fp);
    }
    if (ep->has_parent()) {
      const std::string& name = ep->name();
      file_name[name.size()].insert(name);
      total += name.size();
    }
  });

  log_.info() << "finding duplicate files...";

  set_inode_visitor siv;
  root->accept(siv, true);

  auto im = inode_manager::create(cfg_.block_size_bits);

  for (auto& p : file_hash) {
    if (p.second.size() > 1) {
      std::sort(
          p.second.begin(), p.second.end(),
          [](file const* a, file const* b) { return a->path() < b->path(); });
    }

    auto first = p.second.front();
    {
      auto inode = im->create();
      first->set_inode(inode);
      inode->set_file(first);
    }

    if (p.second.size() > 1) {
      for (auto i = begin(p.second) + 1; i != end(p.second); ++i) {
        (*i)->set_inode(first->get_inode());
        prog.duplicate_files++;
        prog.saved_by_deduplication += (*i)->size();
      }
    }
  }

  log_.info() << "saved " << size_with_unit(prog.saved_by_deduplication)
              << " / " << size_with_unit(prog.original_size) << " in "
              << prog.duplicate_files << "/" << prog.files_found
              << " duplicate files";

  switch (options_.file_order) {
  case file_order_mode::NONE:
    log_.info() << "keeping inode order";
    break;

  case file_order_mode::PATH: {
    log_.info() << "ordering " << im->count() << " inodes by path name...";
    auto ti = log_.timed_info();
    im->order_inodes();
    ti << im->count() << " inodes ordered";
    break;
  }

  case file_order_mode::SCRIPT:
    log_.info() << "ordering " << im->count() << " inodes using script...";
    im->order_inodes(script_);
    break;

  case file_order_mode::SIMILARITY: {
    log_.info() << "ordering " << im->count() << " inodes by similarity...";
    auto ti = log_.timed_info();
    im->order_inodes_by_similarity();
    ti << im->count() << " inodes ordered";
    break;
  }
  }

  log_.info() << "numbering file inodes...";
  im->number_inodes(siv.inode_no());

  log_.info() << "building metadata...";
  std::vector<uint8_t> metadata_vec;
  metadata_writer mw(lgr_, metadata_vec);
  global_entry_data ge_data;
  thrift::metadata::metadata mv2;
  mv2.dir_link_index.resize(siv.inode_no());

  wg_.add_job([&] {
    mw.start_section(section_type::META_TABLEDATA);

    log_.info() << "saving links...";
    names_and_links_visitor nlv(mw, ge_data);
    root->accept(nlv);

    ge_data.index();

    log_.debug() << "link data size = " << mw.section_data_size();

    log_.info() << "saving names...";
    auto name_offset = compress_names_table(mw, file_name);

    log_.debug() << "name data size = " << mw.section_data_size();

    log_.info() << "updating name offsets...";
    root->walk([&](entry* ep) {
      ep->update(ge_data);
      if (auto lp = dynamic_cast<link*>(ep)) {
        mv2.dir_link_index.at(ep->inode_num()) =
            ge_data.get_link_index(lp->linkname());
      }
      if (ep->has_parent()) {
        auto i = name_offset.find(ep->name());
        if (i == name_offset.end()) {
          throw std::runtime_error("offset not found for entry name");
        }
        ep->set_name_offset(i->second);
      }
    });
  });

  log_.info() << "building blocks...";
  block_manager bm(lgr_, prog, cfg_, os_, fsw);

  im->for_each_inode([&](std::shared_ptr<inode> const& ino) {
    prog.current.store(ino.get());
    bm.add_inode(ino);
    prog.inodes_written++;
  });

  prog.sync([&] { prog.current.store(nullptr); });

  log_.debug() << "waiting for block compression to finish...";

  bm.finish_blocks();
  wg_.wait();

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

  // mv2.string_table = std::string(
  //     reinterpret_cast<char const*>(mw.section_data()),
  //     mw.section_data_size());

  // TODO: not sure that's actually needed
  root->set_name(std::string());

  log_.info() << "saving chunks...";
  std::vector<uint32_t> index;
  index.resize(im->count() + 1);
  mv2.chunk_index.resize(im->count() + 1);

  // TODO: we should be able to start this once all blocks have been
  //       submitted for compression
  mw.align(im->chunk_size());
  im->for_each_inode([&](std::shared_ptr<inode> const& ino) {
    index.at(ino->num() - siv.inode_no()) = folly::to<uint32_t>(mw.offset());
    mv2.chunk_index.at(ino->num() - siv.inode_no()) = mv2.chunks.size();
    mw.write(ino->chunks());
    ino->append_chunks(mv2.chunks);
  });

  // insert dummy inode to help determine number of chunks per inode
  index.at(im->count()) = folly::to<uint32_t>(mw.offset());
  mv2.chunk_index.at(im->count()) = mv2.chunks.size();

  mw.finish_section();

  size_t num_chunks = (index.back() - index.front()) / sizeof(chunk_type);

  log_.debug() << "total number of file inodes: " << im->count();
  log_.debug() << "total number of chunks: " << num_chunks;

  log_.info() << "saving chunk index...";
  mw.start_section(section_type::META_CHUNK_INDEX);
  mw.write(index);
  mw.finish_section();

  log_.info() << "saving directories...";
  index.resize(siv.inode_no() + im->count());
  mv2.inode_index.resize(siv.inode_no() + im->count());
  mw.start_section(section_type::META_DIRECTORIES);
  save_directories_visitor sdv(mw, mv2, ge_data, index);
  root->accept(sdv);
  mw.finish_section();

  log_.info() << "saving inode index...";
  mw.start_section(section_type::META_INODE_INDEX);
  mw.write(index);
  mw.finish_section();

  log_.info() << "saving metadata config...";
  mw.start_section(section_type::META_CONFIG);
  meta_config mconf;
  mconf.block_size_bits = folly::to<uint8_t>(im->block_size_bits());
  mconf.de_type = entry_->de_type();
  mconf.unused = 0;
  mconf.inode_count = siv.inode_no() + im->count();
  mconf.orig_fs_size = prog.original_size;
  mconf.chunk_index_offset = siv.inode_no();
  mconf.inode_index_offset = 0;
  mw.write(mconf);
  mw.finish_section();

  fsw.write_metadata(std::move(metadata_vec));

  mv2.uids = ge_data.get_uids();
  mv2.gids = ge_data.get_gids();
  mv2.modes = ge_data.get_modes();
  mv2.names = ge_data.get_names();
  mv2.links = ge_data.get_links();
  mv2.timestamp_base = ge_data.timestamp_base;
  mv2.chunk_index_offset = siv.inode_no();
  mv2.total_fs_size = prog.original_size;

  fsw.write_metadata_v2(freeze_to_buffer(mv2));

  fsw.flush();

  // ::apache::thrift::frozen::freezeToFile(mv2, folly::File("metadata.frozen",
  // O_RDWR | O_CREAT));

  // auto mapping = folly::MemoryMapping("metadata.frozen");

  // ::apache::thrift::frozen::Layout<thrift::metadata::metadata> layout;
  // ::apache::thrift::frozen::schema::Schema schema;
  // auto range = mapping.range();
  // apache::thrift::CompactSerializer::deserialize(range, schema);

  // log_.info() << ::apache::thrift::debugString(schema);

  // auto mapped =
  // ::apache::thrift::frozen::mapFrozen<thrift::metadata::metadata>(std::move(mapping));

  // log_.info() << ::apache::thrift::debugString(mapped.thaw());

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
