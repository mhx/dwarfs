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

// TODO: this should obvs. work everywhere
#ifndef _WIN32
#include <elf.h>
#endif

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace po = boost::program_options;

namespace {

constexpr std::string_view const SOME_CATEGORY{"bla"};

class binary_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class binary_categorizer_ final : public binary_categorizer_base {
 public:
  explicit binary_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  inode_fragments
  categorize(std::filesystem::path const& path, std::span<uint8_t const> data,
             category_mapper const& mapper) const override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
};

std::span<std::string_view const> binary_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      SOME_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
inode_fragments binary_categorizer_<LoggerPolicy>::categorize(
    std::filesystem::path const&,
    std::span<uint8_t const> data [[maybe_unused]],
    category_mapper const& /*mapper*/) const {
  inode_fragments fragments;

#ifndef _WIN32
  auto p = data.data();
  if (data.size() >= EI_NIDENT && ::memcmp(p, ELFMAG, 4) == 0) {
    switch (p[EI_OSABI]) {
    case ELFOSABI_SYSV:       // 0	/* UNIX System V ABI */
    case ELFOSABI_HPUX:       // 1	/* HP-UX */
    case ELFOSABI_NETBSD:     // 2	/* NetBSD.  */
    case ELFOSABI_GNU:        // 3	/* Object uses GNU ELF extensions.  */
    case ELFOSABI_SOLARIS:    // 6	/* Sun Solaris.  */
    case ELFOSABI_AIX:        // 7	/* IBM AIX.  */
    case ELFOSABI_IRIX:       // 8	/* SGI Irix.  */
    case ELFOSABI_FREEBSD:    // 9	/* FreeBSD.  */
    case ELFOSABI_TRU64:      // 10	/* Compaq TRU64 UNIX.  */
    case ELFOSABI_MODESTO:    // 11	/* Novell Modesto.  */
    case ELFOSABI_OPENBSD:    // 12	/* OpenBSD.  */
    case ELFOSABI_ARM_AEABI:  // 64	/* ARM EABI */
    case ELFOSABI_ARM:        // 97	/* ARM */
    case ELFOSABI_STANDALONE: // 255	/* Standalone (embedded) application */
      break;
    }
  }
#endif

  return fragments;
}

template <typename LoggerPolicy>
bool binary_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category, fragment_category) const {
  return false; // TODO
}

class binary_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "binary"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/) const override {
    return make_unique_logging_object<categorizer, binary_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(binary_categorizer_factory)

} // namespace dwarfs
