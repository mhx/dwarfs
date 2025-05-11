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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <dwarfs/history.h>
#include <dwarfs/logger.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/util.h>
#include <dwarfs/utility/rewrite_options.h>
#include <dwarfs/writer/category_resolver.h>
#include <dwarfs/writer/filesystem_writer.h>

#include <dwarfs/reader/internal/filesystem_parser.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>

namespace dwarfs::utility {

void rewrite_filesystem(logger& lgr, dwarfs::reader::filesystem_v2 const& fs,
                        dwarfs::writer::filesystem_writer& fs_writer,
                        dwarfs::writer::category_resolver const& cat_resolver,
                        rewrite_options const& opts) {
  using dwarfs::writer::fragment_category;

  LOG_PROXY(debug_logger_policy, lgr);

  auto parser = fs.get_parser();

  auto& writer = fs_writer.get_internal();

  if (opts.recompress_block) {
    size_t block_no{0};
    parser->rewind();

    while (auto s = parser->next_section()) {
      if (s->type() == section_type::BLOCK) {
        if (auto catstr = fs.get_block_category(block_no)) {
          if (auto cat = cat_resolver.category_value(catstr.value())) {
            writer.check_block_compression(s->compression(),
                                           parser->section_data(*s), cat);
          }
        }
        ++block_no;
      }
    }
  }

  writer.configure_rewrite(parser->filesystem_size(), fs.num_blocks());

  if (auto header = parser->header()) {
    writer.copy_header(*header);
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
        writer.write_compressed_section(*s, parser->section_data(*s));
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

  parser->rewind();

  while (auto s = parser->next_section()) {
    switch (s->type()) {
    case section_type::BLOCK: {
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
            bool is_in_set{opts.recompress_categories.contains(catstr.value())};

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

        writer.write_section(section_type::BLOCK, s->compression(),
                             parser->section_data(*s), cat);
      } else {
        copy_compressed(s, cat);
      }

      ++block_no;
    } break;

    case section_type::METADATA_V2_SCHEMA:
    case section_type::METADATA_V2:
      if (opts.recompress_metadata && !from_none_to_none(s)) {
        log_recompress(s);
        writer.write_section(s->type(), s->compression(),
                             parser->section_data(*s));
      } else {
        copy_compressed(s);
      }
      break;

    case section_type::HISTORY:
      seen_history = true;
      if (opts.enable_history) {
        history hist{opts.history};
        hist.parse(fs.get_history().serialize().span());
        hist.append(opts.command_line_arguments);

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
    hist.append(opts.command_line_arguments);

    LOG_VERBOSE << "adding " << get_section_name(section_type::HISTORY)
                << ", compressing using '"
                << writer.get_compressor(section_type::HISTORY).describe()
                << "'";

    writer.write_history(hist.serialize());
  }

  writer.flush();
}

} // namespace dwarfs::utility
