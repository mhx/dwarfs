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

#include <dwarfs/writer/input_entry_reader.h>

#include <algorithm>
#include <istream>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <dwarfs/file_access.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>

namespace dwarfs::writer {

namespace {

std::istream& get_input_stream(entry_reader_stream& input) {
  return input | match{
                     [](std::istream* s) -> std::istream& { return *s; },
                     [](std::unique_ptr<input_stream>& s) -> std::istream& {
                       return s->is();
                     },
                 };
}

class plain_input_entry_reader_impl final : public input_entry_reader {
 public:
  explicit plain_input_entry_reader_impl(
      std::vector<std::filesystem::path> paths)
      : paths_(std::move(paths)) {}

  plain_input_entry_reader_impl(
      entry_reader_stream input,
      std::filesystem::path const& canonical_input_root, os_access const& os);

  std::optional<input_entry> next() override;

 private:
  std::vector<std::filesystem::path> paths_;
  std::size_t next_{0};
};

plain_input_entry_reader_impl::plain_input_entry_reader_impl(
    entry_reader_stream input,
    std::filesystem::path const& canonical_input_root, os_access const& os) {
  // Read the input list file and rebase absolute paths onto the canonical
  // input path. If we don't do this, the scanner can fail if the input
  // list uses non-canonical absolute paths. Note that we must only rebase
  // the input path prefix of each entry in order to preserve symbolic
  // links in the input list.

  // This keeps track of all aliases of the input path we've seen in the
  // input list so far, so we can test subsequent entries lexically.
  std::vector<std::filesystem::path> lexical_input_aliases{
      canonical_input_root};

  auto rebase_input_list_entry = [&](std::filesystem::path const& p) {
    auto const normalized = p.lexically_normal();

    auto rebase_onto = [&](std::filesystem::path const& alias) {
      auto const rel = normalized.lexically_relative(alias);
      return rel.empty() || rel == "." ? canonical_input_root
                                       : canonical_input_root / rel;
    };

    for (auto const& alias : lexical_input_aliases) {
      if (std::ranges::mismatch(normalized, alias).in2 == alias.end()) {
        return rebase_onto(alias);
      }
    }

    // Check if the start of the lexically normalized path canonicalizes
    // to the canonical input path.
    std::filesystem::path prefix;

    for (auto const& part : normalized) {
      prefix /= part;

      // Skip bare root names (e.g. `C:`).
      if (prefix.has_root_directory() &&
          os.canonical(prefix) == canonical_input_root) {
        lexical_input_aliases.push_back(prefix);
        return rebase_onto(prefix);
      }
    }

    // Not below the input path.
    return p;
  };

  auto& is = get_input_stream(input);
  std::string line;

  while (std::getline(is, line)) {
    std::filesystem::path p(line);
    if (p.has_root_directory()) {
      p = rebase_input_list_entry(p);
    }
    paths_.emplace_back(std::move(p));
  }
}

std::optional<input_entry> plain_input_entry_reader_impl::next() {
  if (next_ == paths_.size()) {
    return std::nullopt;
  }

  return input_entry{std::move(paths_[next_++])};
}

} // namespace

std::unique_ptr<input_entry_reader> plain_input_entry_reader::create(
    entry_reader_stream input,
    std::filesystem::path const& canonical_input_root, os_access const& os) {
  return std::make_unique<plain_input_entry_reader_impl>(
      std::move(input), canonical_input_root, os);
}

std::unique_ptr<input_entry_reader>
plain_input_entry_reader::create(std::vector<std::filesystem::path> paths) {
  return std::make_unique<plain_input_entry_reader_impl>(std::move(paths));
}

} // namespace dwarfs::writer
