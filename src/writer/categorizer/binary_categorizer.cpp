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

#include <cassert>
#include <unordered_map>
#include <vector>

#include <folly/Synchronized.h>
#include <folly/lang/Bits.h>

#include <dwarfs/endian.h>
#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/type_list.h>
#include <dwarfs/writer/categorizer.h>

namespace dwarfs::writer {

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

constexpr std::string_view const ELF_CATEGORY{"binary/elf"};
constexpr std::string_view const PE_CATEGORY{"binary/pe"};

//----- Helper classes --------------------------------------------------------

class subcategory_map {
 public:
  subcategory_map() = default;

  size_t add(uint64_t const key) {
    auto const [it, inserted] =
        reverse_index_.emplace(key, forward_index_.size());
    if (inserted) {
      forward_index_.emplace_back(key);
    }
    return it->second;
  }

  bool less(size_t a, size_t b) const {
    auto const ma = DWARFS_NOTHROW(forward_index_.at(a));
    auto const mb = DWARFS_NOTHROW(forward_index_.at(b));
    return ma < mb;
  }

 private:
  std::vector<uint64_t> forward_index_;
  std::unordered_map<uint64_t, size_t> reverse_index_;
};

class category_subcategory_map {
 public:
  fragment_category
  add(fragment_category::value_type category, uint64_t const subcat_key) {
    return {category, static_cast<fragment_category::value_type>(
                          maps_[category].add(subcat_key))};
  }

  bool less(fragment_category::value_type category, size_t a, size_t b) const {
    auto const it = maps_.find(category);
    assert(it != maps_.end());
    return it->second.less(a, b);
  }

 private:
  std::unordered_map<fragment_category::value_type, subcategory_map> maps_;
};

using sync_subcat_map =
    folly::Synchronized<category_subcategory_map, std::shared_mutex>;

//----- Minimal ELF definitions ------------------------------------------------

struct minimal_elf_header {
  static constexpr size_t MINELF_EI_CLASS{4};
  static constexpr size_t MINELF_EI_DATA{5};
  static constexpr size_t MINELF_EI_VERSION{6};
  static constexpr size_t MINELF_EI_OSABI{7};
  static constexpr size_t MINELF_EI_ABIVERSION{8};

  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;

  bool is_valid() const {
    return e_ident[0] == 0x7f && e_ident[1] == 'E' && e_ident[2] == 'L' &&
           e_ident[3] == 'F' && e_ident[MINELF_EI_VERSION] == 1;
  }

  uint64_t key() const {
    bool const kIsBigEndian = e_ident[MINELF_EI_DATA] == 2;
    auto const type = kIsBigEndian ? folly::Endian::big(e_type)
                                   : folly::Endian::little(e_type);
    auto const machine = kIsBigEndian ? folly::Endian::big(e_machine)
                                      : folly::Endian::little(e_machine);

    return static_cast<uint64_t>(e_ident[MINELF_EI_CLASS]) << 56 |
           static_cast<uint64_t>(e_ident[MINELF_EI_DATA]) << 48 |
           static_cast<uint64_t>(e_ident[MINELF_EI_OSABI]) << 40 |
           static_cast<uint64_t>(e_ident[MINELF_EI_ABIVERSION]) << 32 |
           static_cast<uint64_t>(type) << 16 | static_cast<uint64_t>(machine);
  }

  static bool check(inode_fragments& fragments, std::span<uint8_t const> buf,
                    file_view const& mm, category_mapper const& mapper,
                    sync_subcat_map& subcats) {
    if (buf.size() < sizeof(minimal_elf_header)) {
      return false;
    }

    minimal_elf_header hdr;
    std::memcpy(&hdr, buf.data(), sizeof(hdr));

    if (hdr.is_valid()) {
      auto const cat = mapper(ELF_CATEGORY);
      fragments.emplace_back(subcats.wlock()->add(cat, hdr.key()), mm.size());
      return true;
    }

    return false;
  }
};

//----- Minimal PE definitions -------------------------------------------------

struct minimal_pe_coff_opt {
  uint8_t signature[4];
  uint16le_t machine;
  uint16le_t number_of_sections;
  uint32le_t time_date_stamp;
  uint32le_t pointer_to_symbol_table;
  uint32le_t number_of_symbols;
  uint16le_t size_of_optional_header;
  uint16le_t characteristics;
  uint16le_t opt_magic;

  bool is_pe_coff_opt() const {
    return signature[0] == 'P' && signature[1] == 'E' && signature[2] == 0 &&
           signature[3] == 0;
  }

  uint64_t key() const {
    return static_cast<uint64_t>(opt_magic) << 32 |
           static_cast<uint64_t>(characteristics) << 16 |
           static_cast<uint64_t>(machine);
  }
};

struct minimal_dos_stub {
  uint8_t e_magic[2];
  uint8_t pad[58];
  uint32le_t e_lfanew;

  bool is_valid() const { return e_magic[0] == 'M' && e_magic[1] == 'Z'; }

  static bool check(inode_fragments& fragments, std::span<uint8_t const> buf,
                    file_view const& mm, category_mapper const& mapper,
                    sync_subcat_map& subcats) {
    if (buf.size() < sizeof(minimal_dos_stub)) {
      return false;
    }

    minimal_dos_stub dos;
    std::memcpy(&dos, buf.data(), sizeof(dos));

    if (dos.is_valid() &&
        std::cmp_greater_equal(mm.size(),
                               dos.e_lfanew + sizeof(minimal_pe_coff_opt))) {
      minimal_pe_coff_opt pe;
      std::error_code ec;

      mm.copy_to(pe, dos.e_lfanew, ec);

      if (!ec && pe.is_pe_coff_opt()) {
        auto const cat = mapper(PE_CATEGORY);
        fragments.emplace_back(subcats.wlock()->add(cat, pe.key()), mm.size());
        return true;
      }
    }

    return false;
  }
};

//----- Categorizer implementation ---------------------------------------------

class binary_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class binary_categorizer_ final : public binary_categorizer_base {
 public:
  explicit binary_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  inode_fragments categorize(file_path_info const& path, file_view const& mm,
                             category_mapper const& mapper) const override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  sync_subcat_map mutable subcats_;
};

std::span<std::string_view const> binary_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      ELF_CATEGORY,
      PE_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
inode_fragments binary_categorizer_<LoggerPolicy>::categorize(
    file_path_info const& /*path*/, file_view const& mm,
    category_mapper const& mapper) const {
  inode_fragments fragments;

  if (mm.size() >= 64) {
    std::array<uint8_t, 64> header_buf;

    std::error_code ec;
    mm.copy_to(header_buf, 0, ec);

    if (!ec) {
      using header_types = type_list<minimal_elf_header, minimal_dos_stub>;

      for_each_type_until_true(header_types{}, [&]<typename T>() {
        return T::check(fragments, header_buf, mm, mapper, subcats_);
      });
    }
  }

  return fragments;
}

template <typename LoggerPolicy>
bool binary_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category a, fragment_category b) const {
  assert(a.value() == b.value());
  return subcats_.rlock()->less(a.value(), a.subcategory(), b.subcategory());
}

class binary_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "binary"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/,
         std::shared_ptr<file_access const> const& /*fa*/) const override {
    return make_unique_logging_object<categorizer, binary_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(binary_categorizer_factory)

} // namespace dwarfs::writer
