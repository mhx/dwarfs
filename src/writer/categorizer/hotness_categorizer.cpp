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

#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/command_line.h>
#include <dwarfs/error_fmt.h>
#include <dwarfs/file_access.h>
#include <dwarfs/logger.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/categorizer.h>

namespace dwarfs::writer {

namespace {

constexpr std::string_view const HOTNESS_CATEGORY{"hotness"};

struct hotness_categorizer_config {
  std::string hotness_list;
};

template <typename LoggerPolicy>
class hotness_categorizer_ final : public random_access_categorizer {
 public:
  hotness_categorizer_(logger& lgr, hotness_categorizer_config const& cfg,
                       std::shared_ptr<file_access const> const& fa);

  std::span<std::string_view const> categories() const override;

  inode_fragments categorize(file_path_info const& path, file_view const& mm,
                             category_mapper const& mapper) const override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  std::unordered_set<std::string> hotness_set_;
  std::atomic<bool> mutable warned_no_list_{false};
  hotness_categorizer_config const cfg_;
};

template <typename LoggerPolicy>
hotness_categorizer_<LoggerPolicy>::hotness_categorizer_(
    logger& lgr, hotness_categorizer_config const& cfg,
    std::shared_ptr<file_access const> const& fa)
    : LOG_PROXY_INIT(lgr)
    , cfg_{cfg} {
  auto const& file = cfg_.hotness_list;

  if (!file.empty()) {
    std::error_code ec;
    auto input = fa->open_input(file, ec);

    if (ec) {
      DWARFS_THROWF(runtime_error, "failed to open file '{}': {}", file,
                    ec.message());
    }

    std::string line;
    while (std::getline(input->is(), line)) {
      auto const path = std::filesystem::path{line}.relative_path();
      LOG_DEBUG << "hotness categorizer: adding path '" << path << "'";
      if (!hotness_set_.emplace(path.generic_string()).second) {
        DWARFS_THROWF(runtime_error, "duplicate path in hotness list: '{}'",
                      line);
      }
    }

    if (hotness_set_.empty()) {
      LOG_WARN << "hotness categorizer: empty hotness list";
    }
  }
}

template <typename LoggerPolicy>
std::span<std::string_view const>
hotness_categorizer_<LoggerPolicy>::categories() const {
  static constexpr std::array const s_categories{
      HOTNESS_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
inode_fragments hotness_categorizer_<LoggerPolicy>::categorize(
    file_path_info const& path, file_view const& mm,
    category_mapper const& mapper) const {
  inode_fragments fragments;

  if (!hotness_set_.empty()) {
    auto const rel_path = path.relative_path();

    LOG_DEBUG << "hotness categorizer: checking path '" << rel_path << "' ('"
              << path.full_path() << "')";

    if (hotness_set_.contains(rel_path.generic_string())) {
      fragments.emplace_back(fragment_category(mapper(HOTNESS_CATEGORY)),
                             mm.size());
    }
  } else if (!warned_no_list_) {
    if (cfg_.hotness_list.empty()) {
      LOG_WARN << "hotness categorizer: no hotness list provided";
    }
    warned_no_list_ = true;
  }

  return fragments;
}

template <typename LoggerPolicy>
bool hotness_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category a, fragment_category b) const {
  return a.subcategory() < b.subcategory();
}

class hotness_categorizer_factory : public categorizer_factory {
 public:
  void add_options(cli::options& opts) override {
    opts.group("Hotness categorizer options");
    opts.add("hotness-list", cfg_.hotness_list,
             "file with list of hot file paths")
        .value_name("file");
  }

  std::string_view name() const override { return "hotness"; }

  std::unique_ptr<categorizer>
  create(logger& lgr,
         std::shared_ptr<file_access const> const& fa) const override {
    return make_unique_logging_object<categorizer, hotness_categorizer_,
                                      default_logger_policy>(lgr, cfg_, fa);
  }

 private:
  hotness_categorizer_config cfg_;
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(hotness_categorizer_factory)

} // namespace dwarfs::writer
