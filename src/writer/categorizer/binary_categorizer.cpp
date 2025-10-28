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
#include <cassert>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <folly/Synchronized.h>
#include <folly/lang/Bits.h>

#include <dwarfs/endian.h>
#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/type_list.h>
#include <dwarfs/writer/categorizer.h>

namespace dwarfs::writer {

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

constexpr std::string_view const ELF_CATEGORY{"binary/elf"};
constexpr std::string_view const PE_CATEGORY{"binary/pe"};
constexpr std::string_view const MACHO_HEADER_CATEGORY{"binary/macho-header"};
constexpr std::string_view const MACHO_SECTION_CATEGORY{"binary/macho-section"};

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

//----- Minimal Mach-O definitions ---------------------------------------------

struct minimal_macho_thin_header {
  static constexpr uint32_t MH_MAGIC = 0xfeedface;    // 32-bit BE
  static constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf; // 64-bit BE
  static constexpr uint32_t MH_CIGAM = 0xcefaedfe;    // 32-bit LE
  static constexpr uint32_t MH_CIGAM_64 = 0xcffaedfe; // 64-bit LE

  uint32be_t magic;
  uint32_t cpu_type;
  uint32_t cpu_subtype;
  uint32_t file_type;

  // This works regardless of host endianness
  bool is_valid() const {
    return magic == MH_MAGIC || magic == MH_MAGIC_64 || magic == MH_CIGAM ||
           magic == MH_CIGAM_64;
  }

  uint64_t key() const {
    bool const kIsBigEndian = magic == MH_MAGIC || magic == MH_MAGIC_64;
    bool const kIs64Bit = magic == MH_MAGIC_64 || magic == MH_CIGAM_64;
    auto const cputype = kIsBigEndian ? folly::Endian::big(cpu_type)
                                      : folly::Endian::little(cpu_type);
    auto const subtype = kIsBigEndian ? folly::Endian::big(cpu_subtype)
                                      : folly::Endian::little(cpu_subtype);
    auto const filetype = kIsBigEndian ? folly::Endian::big(file_type)
                                       : folly::Endian::little(file_type);
    return (kIsBigEndian ? 1ULL << 63 : 0ULL) | (kIs64Bit ? 1ULL << 62 : 0ULL) |
           ((static_cast<uint64_t>(filetype) & ((1ULL << 20) - 1)) << 40) |
           ((static_cast<uint64_t>(subtype) & ((1ULL << 20) - 1)) << 20) |
           ((static_cast<uint64_t>(cputype) & ((1ULL << 20) - 1)) << 0);
  }

  static bool check(inode_fragments& fragments, std::span<uint8_t const> buf,
                    file_size_t size, category_mapper const& mapper,
                    sync_subcat_map& subcats) {
    if (buf.size() < sizeof(minimal_macho_thin_header)) {
      return false;
    }

    minimal_macho_thin_header macho;
    std::memcpy(&macho, buf.data(), sizeof(macho));

    if (macho.is_valid()) {
      auto const cat = mapper(MACHO_SECTION_CATEGORY);
      fragments.emplace_back(subcats.wlock()->add(cat, macho.key()), size);
      return true;
    }

    return false;
  }

  static bool check(inode_fragments& fragments, std::span<uint8_t const> buf,
                    file_view const& mm, category_mapper const& mapper,
                    sync_subcat_map& subcats) {
    return check(fragments, buf, mm.size(), mapper, subcats);
  }
};

struct minimal_macho_fat_header {
  static constexpr uint32_t FAT_MAGIC = 0xcafebabe;
  static constexpr uint32_t FAT_MAGIC_64 = 0xcafebabf;

  uint32be_t magic;
  uint32be_t count;

  struct arch32 {
    uint32be_t cputype;
    uint32be_t cpusubtype;
    uint32be_t offset;
    uint32be_t size;
    uint32be_t align;
  };

  struct arch64 {
    uint32be_t cputype;
    uint32be_t cpusubtype;
    uint64be_t offset;
    uint64be_t size;
    uint32be_t align;
    uint32be_t reserved;
  };

  bool is_valid() const { return magic == FAT_MAGIC || magic == FAT_MAGIC_64; }

  template <typename T>
  static bool parse_archs(inode_fragments& fragments, size_t const arch_count,
                          file_view const& mm, category_mapper const& mapper,
                          sync_subcat_map& subcats) {
    fragment_category const header{mapper(MACHO_HEADER_CATEGORY)};

    small_vector<T, 8> archs;
    archs.resize(arch_count);

    std::error_code ec;
    mm.copy_to(std::span{archs.data(), archs.size()},
               sizeof(minimal_macho_fat_header), ec);

    if (ec) {
      return false;
    }

    // sort them by offset, just in case
    std::ranges::sort(archs, std::ranges::less{}, &T::offset);

    file_off_t pos{0};
    file_size_t const end{mm.size()};
    std::array<uint8_t, sizeof(minimal_macho_thin_header)> hdr_buf;
    inode_fragments tmp;

    for (auto const& arch : archs) {
      file_off_t const offset = arch.offset.load();
      file_size_t const size = arch.size.load();

      if (pos > offset) {
        return false;
      }

      if (pos < offset) {
        tmp.emplace_back(header, offset - pos);
        pos = offset;
      }

      pos += size;

      if (pos > end) {
        return false;
      }

      mm.copy_to(hdr_buf, offset, ec);

      if (ec) {
        return false;
      }

      if (!minimal_macho_thin_header::check(tmp, hdr_buf, size, mapper,
                                            subcats)) {
        return false;
      }
    }

    if (pos < end) {
      tmp.emplace_back(header, end - pos);
    }

    tmp.swap(fragments);

    return true;
  };

  static bool check(inode_fragments& fragments, std::span<uint8_t const> buf,
                    file_view const& mm, category_mapper const& mapper,
                    sync_subcat_map& subcats) {
    if (buf.size() < sizeof(minimal_macho_fat_header)) {
      return false;
    }

    minimal_macho_fat_header macho;
    std::memcpy(&macho, buf.data(), sizeof(macho));

    if (macho.is_valid()) {
      if (macho.magic == FAT_MAGIC_64) {
        return parse_archs<arch64>(fragments, macho.count, mm, mapper, subcats);
      }

      return parse_archs<arch32>(fragments, macho.count, mm, mapper, subcats);
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
      MACHO_HEADER_CATEGORY,
      MACHO_SECTION_CATEGORY,
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
      using header_types =
          type_list<minimal_elf_header, minimal_dos_stub,
                    minimal_macho_thin_header, minimal_macho_fat_header>;

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
