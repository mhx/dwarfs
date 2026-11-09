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
  plain_input_entry_reader_impl(
      entry_reader_stream input,
      std::filesystem::path const& canonical_input_root, os_access const& os)
      : input_{std::move(input)}
      , is_{get_input_stream(input_)}
      , canonical_input_root_{canonical_input_root}
      , os_{os}
      , lexical_input_aliases_{canonical_input_root} {}

  std::optional<input_entry> next() override;

 private:
  std::filesystem::path rebase_input_list_entry(std::filesystem::path const& p);

  entry_reader_stream input_;
  std::istream& is_;
  std::filesystem::path canonical_input_root_;
  os_access const& os_;
  // This keeps track of all aliases of the input path we've seen in the
  // input list so far, so we can test subsequent entries lexically.
  std::vector<std::filesystem::path> lexical_input_aliases_;
};

std::filesystem::path plain_input_entry_reader_impl::rebase_input_list_entry(
    std::filesystem::path const& p) {
  // Rebase only the input path prefix, preserve symlinks in the suffix.
  auto const normalized = p.lexically_normal();

  auto rebase_onto = [&](std::filesystem::path const& alias) {
    auto const rel = normalized.lexically_relative(alias);
    return rel.empty() || rel == "." ? canonical_input_root_
                                     : canonical_input_root_ / rel;
  };

  for (auto const& alias : lexical_input_aliases_) {
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
        os_.canonical(prefix) == canonical_input_root_) {
      lexical_input_aliases_.push_back(prefix);
      return rebase_onto(prefix);
    }
  }

  // Not below the input path.
  return p;
}

std::optional<input_entry> plain_input_entry_reader_impl::next() {
  std::string line;

  if (!std::getline(is_, line)) {
    return std::nullopt;
  }

  std::filesystem::path p(line);

  if (p.has_root_directory()) {
    p = rebase_input_list_entry(p);
  }

  return input_entry{std::move(p)};
}

} // namespace

std::unique_ptr<input_entry_reader> plain_input_entry_reader::create(
    entry_reader_stream input,
    std::filesystem::path const& canonical_input_root, os_access const& os) {
  return std::make_unique<plain_input_entry_reader_impl>(
      std::move(input), canonical_input_root, os);
}

} // namespace dwarfs::writer
