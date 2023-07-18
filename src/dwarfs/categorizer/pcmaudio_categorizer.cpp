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

#include <array>
#include <cstring>
#include <map>
#include <stack>
#include <unordered_set>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace po = boost::program_options;

namespace {

constexpr std::string_view const AIFF_CATEGORY{"aiff"};
constexpr std::string_view const WAV_CATEGORY{"wav"};

class pcmaudio_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class pcmaudio_categorizer_ final : public pcmaudio_categorizer_base {
 public:
  pcmaudio_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  inode_fragments
  categorize(std::filesystem::path const& path, std::span<uint8_t const> data,
             category_mapper const& mapper) const override;

  bool is_single_fragment() const override { return false; }

  folly::dynamic category_metadata(fragment_category) const override {
    // TODO
    return folly::dynamic();
  }

 private:
  LOG_PROXY_DECL(LoggerPolicy);
};

std::span<std::string_view const>
pcmaudio_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      AIFF_CATEGORY,
      WAV_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
inode_fragments pcmaudio_categorizer_<LoggerPolicy>::categorize(
    std::filesystem::path const&,
    std::span<uint8_t const> data [[maybe_unused]],
    category_mapper const& /*mapper*/) const {
  inode_fragments fragments;

  return fragments;
}

class pcmaudio_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "pcmaudio"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/) const override {
    return make_unique_logging_object<categorizer, pcmaudio_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(pcmaudio_categorizer_factory)

} // namespace dwarfs
