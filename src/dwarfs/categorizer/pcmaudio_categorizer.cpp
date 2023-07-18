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

#include <folly/Synchronized.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace po = boost::program_options;

namespace {

constexpr std::string_view const PCMAUDIO_CATEGORY{"pcmaudio"};

enum class endianness : uint8_t {
  BIG,
  LITTLE,
};
enum class signedness : uint8_t {
  SIGNED,
  UNSIGNED,
};

char const* endianness_string(endianness e) {
  switch (e) {
  case endianness::BIG:
    return "big";
  case endianness::LITTLE:
    return "little";
  }
}

char const* signedness_string(signedness s) {
  switch (s) {
  case signedness::SIGNED:
    return "signed";
  case signedness::UNSIGNED:
    return "unsigned";
  }
}

struct pcmaudio_metadata {
  endianness sample_endianness;
  signedness sample_signedness;
  uint8_t bits_per_sample;
  uint16_t number_of_channels;

  //// Sample rate should be irrelevant
  // uint32_t samples_per_second;

  auto operator<=>(pcmaudio_metadata const&) const = default;
};

class pcmaudio_metadata_store {
 public:
  pcmaudio_metadata_store() = default;

  size_t add(pcmaudio_metadata const& m) {
    auto it = reverse_index_.find(m);
    if (it == reverse_index_.end()) {
      auto r = reverse_index_.emplace(m, forward_index_.size());
      assert(r.second);
      forward_index_.emplace_back(m);
      it = r.first;
    }
    return it->second;
  }

  folly::dynamic lookup(size_t ix) const {
    auto const& m = DWARFS_NOTHROW(forward_index_.at(ix));
    folly::dynamic obj = folly::dynamic::object;
    obj.insert("endianness", endianness_string(m.sample_endianness));
    obj.insert("signedness", signedness_string(m.sample_signedness));
    obj.insert("bits_per_sample", m.bits_per_sample);
    obj.insert("number_of_channels", m.number_of_channels);
    return obj;
  }

 private:
  std::vector<pcmaudio_metadata> forward_index_;
  std::map<pcmaudio_metadata, size_t> reverse_index_;
};

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

  folly::dynamic category_metadata(fragment_category c) const override {
    DWARFS_CHECK(c.has_subcategory(), "expected pcmaudio to have subcategory");
    return meta_.rlock()->lookup(c.subcategory());
  }

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  folly::Synchronized<pcmaudio_metadata_store> mutable meta_;
};

std::span<std::string_view const>
pcmaudio_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      PCMAUDIO_CATEGORY,
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
