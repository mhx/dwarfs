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

#include <vector>

#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/rewrite_options.h>
#include <dwarfs/writer/category_resolver.h>
#include <dwarfs/writer/filesystem_writer.h>

#include <dwarfs/reader/internal/filesystem_parser.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>
#include <dwarfs/writer/internal/metadata_builder.h>
#include <dwarfs/writer/internal/metadata_freezer.h>

#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::utility {

namespace {

/*

In order to be able to change the block size, we need to first build a list
of all blocks, along with their categories *and* category-specific metadata.
Only blocks in the same category and with the same metadata are eligible for
merging. While category/metadata is mostly irrelevant for splitting, splitting
requires us to know the compression constraints (i.e. the granularity of the
data) so we can split at the correct boundaries.

Granularity also makes splitting/merging more complicated, as we potentially
cannot simply split a block because one of the new blocks would be larger than
the block size. In which case we must move the excess data to the next block,
and so on. Simpilarly, when merging blocks, we can potentially fill up the
block with data from the next block.

So, ultimately, we need to define for each block in the rewritten filesystem
image the chunks of which it is made up. This mapping will not only be used
to build the new blocks, but also to rebuild the metadata. In the metadata,
both the chunks *and* the chunk table must be updated, since individual chunks
can be either merged or split as well. If we want to be super accurate, we
would also need to update the inode size cache; but this would only be relevant
if we go from a really large block size to a really small one. Then again, it
shouldn't be too hard to update the cache. What we *definitely* need to update
is the `block_categories` as well as the `block_category_metadata` tables in
the metadata.

So, what we need:

- A list of all blocks, along with their categories and metadata

*/

struct block_info {
  size_t block{};
  size_t uncompressed_size{};
  std::optional<dwarfs::internal::fs_section> section;
  std::optional<std::string> category_name;
  std::optional<std::string> metadata;
  std::optional<compression_constraints> constraints;
};

/*

- An algorithm for splitting/merging that outputs the new block positions
  (numbers) and the chunks that make up each block

    struct block_chunk {  // see metadata_builder.h
      size_t block;
      size_t offset;
      size_t size;
    };

*/

struct new_block_mapping {
  size_t block{};
  size_t size{};
  std::vector<dwarfs::writer::internal::block_chunk> chunks{};
  std::optional<std::string> category_name;
  std::optional<std::string> metadata;
};

/*

- The algorithm should be deterministic. It doesn't have to be reversible,
  i.e. splitting then merging or merging then splitting doesn't have to
  yield the same result (or even the original filesystem image). But when
  splitting or merging, the result should *always* be the same given the
  same input. That means we *could* actually consider grouping blocks by
  category and metadata in the output.

  TODO: Check if we've gone from a compression with constraints to one
        without (i.e. granularity 3 -> 1) and want to go back to the
        original compression *without* a block size change, that should
        fail early.

We need two new features to support this:

- `filesystem_v2` must allow reading raw block data (i.e. not file-based).
  That way, we can easily make use of the block cache while re-composing
  the blocks.

- `filesystem_writer` must allow delayed reading of the block data. We
  can hopefully refactor the `rewritten_fsblock` to support this.

How does the remapping process work in the metadata builder?

The chunk_table is just a list of the first chunk of each regular file
inode, plus a sentinel at the end. Basically, we need to traverse the
chunk_table and the chunks it references and build new versions of the
chunk_table and chunks using the new blocks.

To build a new chunk from an old chunk, we must be able figure out which
new blocks an old block is mapped to. This is sort of the opposite of
`mapped_block_info`, where we have stored which chunks of old blocks
make up a new block. So we need a second mapping:

    struct block_mapping {  // see metadata_builder.h
      size_t old_block;
      std::vector<block_chunk> chunks;
    };

*/

struct rw_block_mappings {
  std::vector<new_block_mapping> new_to_old;
  std::vector<dwarfs::writer::internal::block_mapping> old_to_new;
};

rw_block_mappings build_block_mappings(std::span<block_info const> blocks,
                                       size_t const block_size) {
  using stream_id =
      std::pair<std::optional<std::string>, std::optional<std::string>>;
  std::vector<std::vector<size_t>> streams;
  std::map<stream_id, size_t> stream_map;

  for (auto const& b : blocks) {
    stream_id id{b.category_name, b.metadata};
    auto [it, inserted] = stream_map.try_emplace(id, streams.size());
    if (inserted) {
      streams.emplace_back();
    }
    streams[it->second].push_back(b.block);
  }

  rw_block_mappings result;

  for (auto const& stream : streams) {
    size_t granularity{1};

    if (auto const& cc = blocks[stream[0]].constraints; cc && cc->granularity) {
      granularity = cc->granularity.value();
    }

    size_t const max_stream_block_size{granularity *
                                       (block_size / granularity)};

    std::vector<new_block_mapping> mapped;

    for (size_t block : stream) {
      result.old_to_new.push_back({.old_block = block});
      auto& old_to_new = result.old_to_new.back();
      auto const& b = blocks[block];
      size_t offset{0};

      while (offset < b.uncompressed_size) {
        if (mapped.empty() || mapped.back().size == max_stream_block_size) {
          mapped.push_back({.block = result.new_to_old.size() + mapped.size(),
                            .category_name = b.category_name,
                            .metadata = b.metadata});
        }

        auto& m = mapped.back();
        size_t const chunk_size{std::min(b.uncompressed_size - offset,
                                         max_stream_block_size - m.size)};

        old_to_new.chunks.push_back(
            {.block = m.block, .offset = m.size, .size = chunk_size});

        m.chunks.push_back(
            {.block = block, .offset = offset, .size = chunk_size});

        m.size += chunk_size;
        offset += chunk_size;
      }
    }

    std::ranges::move(mapped, std::back_inserter(result.new_to_old));
  }

  std::ranges::sort(result.old_to_new, [](auto const& a, auto const& b) {
    return a.old_block < b.old_block;
  });

  return result;
}

std::string block_mappings_to_string(rw_block_mappings const& mapped) {
  std::ostringstream oss;
  for (auto const& m : mapped.new_to_old) {
    oss << "new block " << m.block << " (size " << m.size;
    if (m.category_name) {
      oss << ", category " << *m.category_name;
    }
    if (m.metadata) {
      oss << ", metadata " << *m.metadata;
    }
    oss << "):\n";
    for (auto const& c : m.chunks) {
      oss << "  chunk: old block " << c.block << ", offset " << c.offset
          << ", size " << c.size << "\n";
    }
  }
  for (auto const& m : mapped.old_to_new) {
    oss << "old block " << m.old_block << ":\n";
    for (auto const& c : m.chunks) {
      oss << "  chunk: new block " << c.block << ", offset " << c.offset
          << ", size " << c.size << "\n";
    }
  }
  return oss.str();
}

} // namespace

void rewrite_filesystem(
    logger& lgr, dwarfs::reader::filesystem_v2 const& fs,
    dwarfs::writer::filesystem_writer& fs_writer,
    dwarfs::writer::category_resolver const& cat_resolver,
    rewrite_options const& opts,
    std::function<void(library_dependencies&)> const& extra_deps) {
  using dwarfs::writer::fragment_category;

  LOG_PROXY(debug_logger_policy, lgr);

  if (opts.change_block_size) {
    DWARFS_CHECK(opts.recompress_block,
                 "change_block_size requires recompress_block");
    DWARFS_CHECK(opts.recompress_metadata,
                 "change_block_size requires recompress_metadata");
    DWARFS_CHECK(opts.rebuild_metadata,
                 "change_block_size requires rebuild_metadata");
  }

  if (fs.has_sparse_files() && !opts.rebuild_metadata->enable_sparse_files) {
    DWARFS_THROW(
        runtime_error,
        "cannot disable sparse files when the input filesystem uses them");
  }

  std::vector<block_info> blocks;
  rw_block_mappings mapped_blocks;

  auto parser = fs.get_parser();

  auto& writer = fs_writer.get_internal();

  if (opts.recompress_block) {
    parser->rewind();

    {
      auto tv = LOG_TIMED_VERBOSE;
      size_t block_no{0};

      while (auto s = parser->next_section()) {
        if (s->type() == section_type::BLOCK) {
          dwarfs::writer::internal::block_compression_info bci;
          auto catstr = fs.get_block_category(block_no);
          std::optional<fragment_category::value_type> cat;
          std::optional<std::string> cat_metadata;

          if (catstr) {
            cat = cat_resolver.category_value(catstr.value());
          }

          if (auto cm = fs.get_block_category_metadata(block_no)) {
            cat_metadata = cm->dump();
          }

          auto segment = parser->segment(*s);

          writer.check_block_compression(
              s->compression(), s->data(segment), cat, cat_metadata,
              opts.change_block_size ? &bci : nullptr);

          if (opts.change_block_size) {
            DWARFS_CHECK(block_no == blocks.size(),
                         fmt::format("block_no ({}) != blocks.size() ({})",
                                     block_no, blocks.size()));
            LOG_DEBUG << "adding block " << block_no
                      << " uncompressed size: " << bci.uncompressed_size;
            auto& info = blocks.emplace_back();
            info.block = block_no;
            info.uncompressed_size = bci.uncompressed_size;
            info.section = s;
            info.category_name = catstr;
            info.metadata = bci.metadata;
            info.constraints = bci.constraints;
          }

          ++block_no;
        }
      }

      tv << "checked compression for " << block_no << " blocks";
    }

    if (opts.change_block_size) {
      {
        auto tv = LOG_TIMED_VERBOSE;

        mapped_blocks =
            build_block_mappings(blocks, opts.change_block_size.value());

        tv << "mapped " << blocks.size() << " source blocks to "
           << mapped_blocks.new_to_old.size() << " target blocks";
      }

      LOG_DEBUG << block_mappings_to_string(mapped_blocks);
    }
  }

  writer.configure_rewrite(parser->filesystem_size(),
                           opts.change_block_size
                               ? mapped_blocks.new_to_old.size()
                               : fs.num_blocks());

  if (auto header = parser->header()) {
    writer.copy_header(std::move(*header));
  }

  size_t block_no{0};
  bool seen_history{false};

  auto log_rewrite =
      [&](bool compressing, auto const& s,
          std::optional<fragment_category::value_type> const& cat) {
        auto prefix = compressing ? "recompressing" : "copying";
        std::string catinfo;
        std::string compinfo;
        if (cat) {
          catinfo = fmt::format(", {}", cat_resolver.category_name(*cat));
        }
        if (compressing) {
          compinfo = fmt::format(
              " using '{}'", writer.get_compressor(s->type(), cat).describe());
        }
        LOG_VERBOSE << prefix << " " << size_with_unit(s->length()) << " "
                    << get_section_name(s->type()) << " ("
                    << get_compression_name(s->compression()) << catinfo << ")"
                    << compinfo;
      };

  auto log_recompress =
      [&](auto const& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) { log_rewrite(true, s, cat); };

  auto copy_compressed =
      [&](auto const& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) {
        log_rewrite(false, s, cat);
        writer.write_compressed_section(*s, parser->segment(*s));
      };

  auto from_none_to_none =
      [&](auto const& s,
          std::optional<fragment_category::value_type> const& cat =
              std::nullopt) {
        if (s->compression() == compression_type::NONE) {
          auto& bc = writer.get_compressor(s->type(), cat);
          if (bc.type() == compression_type::NONE) {
            return true;
          }
        }
        return false;
      };

  if (opts.change_block_size) {
    for (auto const& m : mapped_blocks.new_to_old) {
      std::optional<fragment_category::value_type> cat;

      if (m.category_name) {
        cat = cat_resolver.category_value(m.category_name.value());
      }

      writer.rewrite_block(
          [&] {
            auto data = malloc_byte_buffer::create_reserve(m.size);
            for (auto const& c : m.chunks) {
              auto range =
                  fs.read_raw_block_data(c.block, c.offset, c.size).get();
              data.append(range.data(), range.size());
            }
            DWARFS_CHECK(data.size() == m.size,
                         fmt::format("data size {} != expected size {}",
                                     data.size(), m.size));
            return std::pair{data.share(), m.metadata};
          },
          m.size, cat);
    }
  }

  parser->rewind();

  while (auto s = parser->next_section()) {
    switch (s->type()) {
    case section_type::BLOCK:
      if (!opts.change_block_size) {
        std::optional<fragment_category::value_type> cat;
        bool recompress_block{opts.recompress_block};

        if (recompress_block) {
          auto catstr = fs.get_block_category(block_no);

          if (catstr) {
            cat = cat_resolver.category_value(catstr.value());

            if (!cat) {
              LOG_ERROR << "unknown category '" << catstr.value()
                        << "' for block " << block_no;
            }

            if (!opts.recompress_categories.empty()) {
              bool is_in_set{
                  opts.recompress_categories.contains(catstr.value())};

              recompress_block =
                  opts.recompress_categories_exclude ? !is_in_set : is_in_set;
            }
          }
        }

        if (recompress_block && from_none_to_none(s, cat)) {
          recompress_block = false;
        }

        if (recompress_block) {
          log_recompress(s, cat);

          std::optional<std::string> cat_metadata;

          if (auto cm = fs.get_block_category_metadata(block_no)) {
            cat_metadata = cm->dump();
          }

          writer.rewrite_section(*s, parser->segment(*s), cat, cat_metadata);
        } else {
          copy_compressed(s, cat);
        }

        ++block_no;
      }
      break;

    case section_type::METADATA_V2_SCHEMA:
    case section_type::METADATA_V2:
      if (opts.rebuild_metadata) {
        DWARFS_CHECK(opts.recompress_metadata,
                     "rebuild_metadata requires recompress_metadata");
        if (s->type() == section_type::METADATA_V2) {
          using namespace dwarfs::writer::internal;

          auto md = fs.unpacked_metadata();
          auto fsopts = fs.thawed_fs_options();
          auto builder =
              metadata_builder(lgr, std::move(*md), fsopts.get(), fs.version(),
                               opts.rebuild_metadata.value());

          if (opts.change_block_size) {
            builder.set_block_size(opts.change_block_size.value());
            builder.remap_blocks(mapped_blocks.old_to_new,
                                 mapped_blocks.new_to_old.size());
          }

          auto [schema, data] =
              metadata_freezer(LOG_GET_LOGGER).freeze(builder.build());

          writer.write_metadata_v2_schema(std::move(schema));
          writer.write_metadata_v2(std::move(data));
        }
      } else {
        if (opts.recompress_metadata && !from_none_to_none(s)) {
          log_recompress(s);
          writer.rewrite_section(*s, parser->segment(*s));
        } else {
          copy_compressed(s);
        }
      }
      break;

    case section_type::HISTORY:
      seen_history = true;
      if (opts.enable_history) {
        history hist{opts.history};
        hist.parse(fs.get_history().serialize().span());
        hist.append(opts.command_line_arguments, extra_deps);

        LOG_VERBOSE << "updating " << get_section_name(s->type()) << " ("
                    << get_compression_name(s->compression())
                    << "), compressing using '"
                    << writer.get_compressor(s->type()).describe() << "'";

        writer.write_history(hist.serialize());
      } else {
        LOG_VERBOSE << "removing " << get_section_name(s->type());
      }
      break;

    case section_type::SECTION_INDEX:
      // this will be automatically added by the filesystem_writer
      break;

    default:
      // verbatim copy everything else
      copy_compressed(s);
      break;
    }
  }

  if (!seen_history && opts.enable_history) {
    history hist{opts.history};
    hist.append(opts.command_line_arguments, extra_deps);

    LOG_VERBOSE << "adding " << get_section_name(section_type::HISTORY)
                << ", compressing using '"
                << writer.get_compressor(section_type::HISTORY).describe()
                << "'";

    writer.write_history(hist.serialize());
  }

  writer.flush();
}

} // namespace dwarfs::utility
