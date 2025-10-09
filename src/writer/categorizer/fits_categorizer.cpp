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
#include <bit>
#include <cassert>
#include <charconv>
#include <cstring>
#include <functional>
#include <map>
#include <shared_mutex>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <folly/Synchronized.h>
#include <folly/lang/Bits.h>

#include <range/v3/algorithm/fold_left.hpp>
#include <range/v3/view/chunk.hpp>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/map_util.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/compression_metadata_requirements.h>

template <>
struct fmt::formatter<std::endian> : formatter<std::string_view> {
  template <typename FormatContext>
  auto format(std::endian e, FormatContext& ctx) const {
    std::string_view sv{"<unknown endian>"};
    switch (e) {
    case std::endian::little: // unused, there are no little-endian FITS files
      sv = "little";
      break;
    case std::endian::big:
      sv = "big";
      break;
    default:
      DWARFS_PANIC("internal error: unhandled endianness value");
    }
    return formatter<std::string_view>::format(sv, ctx);
  }
};

namespace dwarfs::writer {

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

constexpr std::string_view const METADATA_CATEGORY{"fits/metadata"};
constexpr std::string_view const IMAGE_CATEGORY{"fits/image"};

constexpr file_size_t const FITS_SIZE_GRANULARITY{2880};

std::optional<std::endian> parse_endian(std::string_view e) {
  static std::unordered_map<std::string_view, std::endian> const lookup{
      {"big", std::endian::big},
      {"little", std::endian::little},
  };
  return get_optional(lookup, e);
}

std::optional<std::endian> parse_endian_dyn(nlohmann::json const& e) {
  return parse_endian(e.get<std::string>());
}

struct fits_info {
  unsigned pixel_bits{};
  unsigned component_count{};
  unsigned unused_lsb_count{};
  file_range header;
  file_range imagedata;
  file_range footer;
};

std::string_view trim(std::string_view sv) {
  if (auto const pos = sv.find_first_not_of(' ');
      pos != std::string_view::npos) {
    sv.remove_prefix(pos);
  }
  if (auto const pos = sv.find_last_not_of(' ');
      pos != std::string_view::npos) {
    sv.remove_suffix(sv.size() - pos - 1);
  }
  return sv;
}

template <std::unsigned_integral T>
T merge_sample_bits(std::span<uint8_t const> data);

template <std::unsigned_integral T>
unsigned get_unused_lsb_count(file_extents_iterable const& extents);

template <typename T>
T fold_left_bit_or(std::span<uint8_t const> data) {
  assert(data.size_bytes() % sizeof(T) == 0);
  std::span<T const> span{reinterpret_cast<T const*>(data.data()),
                          data.size_bytes() / sizeof(T)};
  return ranges::fold_left(span, T{}, std::bit_or{});
}

template <>
uint16_t merge_sample_bits<uint16_t>(std::span<uint8_t const> imagedata) {
  static constexpr uint64_t const kLsbMask{std::endian::native ==
                                                   std::endian::little
                                               ? UINT64_C(0x0100010001000100)
                                               : UINT64_C(0x0001000100010001)};
  static constexpr size_t const kAlignment{64}; // for AVX512

  uint16_t b16{0};

  if (auto ptr = reinterpret_cast<uintptr_t>(imagedata.data());
      ptr % kAlignment != 0) [[unlikely]] {
    if (ptr % sizeof(uint16_t) != 0) [[unlikely]] {
      throw std::runtime_error("unaligned imagedata");
    }

    auto preamble_size = kAlignment - (ptr % kAlignment);

    b16 = fold_left_bit_or<uint16_t>(imagedata.subspan(0, preamble_size));

    imagedata = imagedata.subspan(preamble_size);
  }

  auto p = reinterpret_cast<uint64_t const*>(
      __builtin_assume_aligned(imagedata.data(), kAlignment));
  size_t size = imagedata.size_bytes() / kAlignment;

  alignas(kAlignment) std::array<uint64_t, kAlignment / sizeof(uint64_t)> b512;
  std::ranges::fill(b512, 0);

  for (size_t i = 0; i < size; ++i) {
    for (size_t k = 0; k < b512.size(); ++k) {
      b512[k] |= p[b512.size() * i + k];
    }
    if (b512[0] & kLsbMask) {
      return UINT16_MAX; // all bits used
    }
  }

  uint64_t b64 = ranges::fold_left(b512, 0, std::bit_or{});
  b16 |= (b64 >> 48) | (b64 >> 32) | (b64 >> 16) | b64;
  b16 |= fold_left_bit_or<uint16_t>(imagedata.subspan(
      size * kAlignment, imagedata.size_bytes() % kAlignment));

  return folly::Endian::big(b16);
}

template <>
unsigned get_unused_lsb_count<uint16_t>(file_extents_iterable const& extents) {
  uint16_t b16{0};

  for (auto const& ext : extents) {
    if (ext.kind() == extent_kind::data) {
      for (auto const& seg : ext.segments()) {
        b16 |= merge_sample_bits<uint16_t>(seg.span<uint8_t>());
        if (b16 & 1) {
          return 0; // short-circuit: LSB is used
        }
      }
    }
  }

  return std::countr_zero(b16);
}

bool has_non_printable_ascii(std::string_view s) {
  auto bad = [](unsigned char ch) {
    return (ch - 32U) > 94U;
  }; // ch < 32 or ch > 126
  return std::ranges::any_of(s, bad);
}

std::optional<fits_info> parse_fits(file_view const& mm) {
  static constexpr int kColumnWidth{80};

  fits_info fi;
  fi.component_count = 1;

  bool is_fits = false;
  int pixel_bits = -1;
  int xdim = -1;
  int ydim = -1;

  file_off_t filepos = 0;
  size_t value_count = 0;

  for (;; filepos += kColumnWidth) {
    std::error_code ec;
    auto row = mm.read_string(filepos, kColumnWidth, ec);

    if (ec) {
      break;
    }

    // FITS requires that the header is printable US-ASCII
    if (has_non_printable_ascii(row)) {
      break;
    }

#if defined(__apple_build_version__) && __apple_build_version__ < 14000000
    std::string_view rv{row.data(), row.size()};
#else
    std::string_view rv{row.begin(), row.end()};
#endif
    auto const keyword = trim(rv.substr(0, 8));

    if (keyword == "COMMENT" || keyword == "HISTORY" || keyword == "CONTINUE") {
      continue;
    }

    if (filepos >= 2 * FITS_SIZE_GRANULARITY && value_count == 0) {
      // this is *very* unlikely to be a valid FITS file
      break;
    }

    if (keyword.empty()) {
      continue;
    }

    if (keyword == "END") {
      if (!is_fits || xdim == -1 || ydim == -1 || pixel_bits == -1) {
        return std::nullopt;
      }

      auto const header_frames =
          (filepos + FITS_SIZE_GRANULARITY - 1) / FITS_SIZE_GRANULARITY;
      auto const range = mm.range();
      fi.header = range.subrange(0, header_frames * FITS_SIZE_GRANULARITY);
      fi.imagedata =
          range.subrange(fi.header.size(), xdim * ydim * sizeof(uint16_t));
      fi.footer = mm.range().subrange(fi.header.size() + fi.imagedata.size());
      fi.pixel_bits = static_cast<unsigned>(pixel_bits);
      fi.unused_lsb_count =
          get_unused_lsb_count<uint16_t>(mm.extents(fi.imagedata));
      return fi;
    }

    if (rv[8] != '=' || rv[9] != ' ') {
      continue;
    }

    auto value = rv.substr(9);

    ++value_count;

    if (auto pos = value.find('/'); pos != std::string_view::npos) {
      value.remove_suffix(value.size() - pos);
    }

    value = trim(value);

    if (keyword == "SIMPLE") {
      if (value != "T") {
        return std::nullopt;
      }
      is_fits = true;
    } else if (keyword == "BITPIX") {
      std::from_chars(value.data(), value.data() + value.size(), pixel_bits);
      if (pixel_bits != 16) {
        return std::nullopt;
      }
    } else if (keyword == "NAXIS") {
      if (value != "2") {
        return std::nullopt;
      }
    } else if (keyword == "NAXIS1") {
      std::from_chars(value.data(), value.data() + value.size(), xdim);
    } else if (keyword == "NAXIS2") {
      std::from_chars(value.data(), value.data() + value.size(), ydim);
    } else if (keyword == "BAYERPAT") {
      fi.component_count = 2;
    }
  }

  return std::nullopt;
}

struct fits_metadata {
  std::endian endianness;
  uint8_t bytes_per_sample;
  uint8_t unused_lsb_count;
  uint16_t component_count;

  auto operator<=>(fits_metadata const&) const = default;

  bool check() const {
    // make sure we're supporting a reasonable subset

    if (component_count == 0) {
      return false;
    }

    if (bytes_per_sample != 2) { // TODO
      return false;
    }

    if (unused_lsb_count > 8) {
      return false;
    }

    if (endianness != std::endian::big) { // TODO
      return false;
    }

    return true;
  }
};

std::ostream& operator<<(std::ostream& os, fits_metadata const& m) {
  os << "[" << fmt::format("{}", m.endianness) << "-endian, "
     << "bytes=" << static_cast<int>(m.bytes_per_sample) << ", "
     << "unused=" << static_cast<int>(m.unused_lsb_count) << ", "
     << "components=" << static_cast<int>(m.component_count) << "]";
  return os;
}

class fits_metadata_store {
 public:
  fits_metadata_store() = default;

  size_t add(fits_metadata const& m) {
    auto it = reverse_index_.find(m);
    if (it == reverse_index_.end()) {
      auto r = reverse_index_.emplace(m, forward_index_.size());
      assert(r.second);
      forward_index_.emplace_back(m);
      it = r.first;
    }
    return it->second;
  }

  std::string lookup(size_t ix) const {
    auto const& m = DWARFS_NOTHROW(forward_index_.at(ix));
    nlohmann::json obj{
        {"endianness", fmt::format("{}", m.endianness)},
        {"bytes_per_sample", m.bytes_per_sample},
        {"unused_lsb_count", m.unused_lsb_count},
        {"component_count", m.component_count},
    };
    return obj.dump();
  }

  bool less(size_t a, size_t b) const {
    auto const& ma = DWARFS_NOTHROW(forward_index_.at(a));
    auto const& mb = DWARFS_NOTHROW(forward_index_.at(b));
    return ma < mb;
  }

 private:
  std::vector<fits_metadata> forward_index_;
  std::map<fits_metadata, size_t> reverse_index_;
};

class fits_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class fits_categorizer_ final : public fits_categorizer_base {
 public:
  explicit fits_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {
    image_req_.add_set("endianness", &fits_metadata::endianness,
                       parse_endian_dyn);
    image_req_.add_set<int>("bytes_per_sample",
                            &fits_metadata::bytes_per_sample);
    image_req_.add_range<int>("unused_lsb_count",
                              &fits_metadata::unused_lsb_count);
    image_req_.add_range<int>("component_count",
                              &fits_metadata::component_count);
  }

  inode_fragments categorize(file_path_info const& path, file_view const& mm,
                             category_mapper const& mapper) const override;

  std::string category_metadata(std::string_view category_name,
                                fragment_category c) const override {
    if (category_name == IMAGE_CATEGORY) {
      DWARFS_CHECK(c.has_subcategory(), "expected IMAGE to have subcategory");
      return meta_.rlock()->lookup(c.subcategory());
    }
    return {};
  }

  void set_metadata_requirements(std::string_view category_name,
                                 std::string_view requirements) override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  bool check_metadata(fits_metadata const& meta, fs::path const& path) const;

  LOG_PROXY_DECL(LoggerPolicy);
  folly::Synchronized<fits_metadata_store, std::shared_mutex> mutable meta_;
  compression_metadata_requirements<fits_metadata> image_req_;
};

std::span<std::string_view const> fits_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      METADATA_CATEGORY,
      IMAGE_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
bool fits_categorizer_<LoggerPolicy>::check_metadata(
    fits_metadata const& meta, fs::path const& path) const {
  if (!meta.check()) {
    LOG_WARN << path << ": metadata check failed: " << meta;
    return false;
  }

  try {
    image_req_.check(meta);
  } catch (std::exception const& e) {
    LOG_WARN << path << ": " << e.what();
    return false;
  }

  LOG_TRACE << path << ": meta=" << meta;

  return true;
}

template <typename LoggerPolicy>
inode_fragments fits_categorizer_<LoggerPolicy>::categorize(
    file_path_info const& path, file_view const& mm,
    category_mapper const& mapper) const {
  inode_fragments fragments;

  auto const file_size = mm.size();

  if (file_size >= 2 * FITS_SIZE_GRANULARITY &&
      file_size % FITS_SIZE_GRANULARITY == 0) {
    if (auto fi = parse_fits(mm)) {
      if (fi->pixel_bits == 16) {
        fits_metadata meta;
        meta.endianness = std::endian::big;
        meta.bytes_per_sample = 2;
        meta.unused_lsb_count = fi->unused_lsb_count;
        meta.component_count = fi->component_count;

        if (check_metadata(meta, path.full_path())) {
          auto subcategory = meta_.wlock()->add(meta);
          fragments.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                                 fi->header.size());
          fragments.emplace_back(
              fragment_category(mapper(IMAGE_CATEGORY), subcategory),
              fi->imagedata.size());
          if (!fi->footer.empty()) {
            fragments.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                                   fi->footer.size());
          }
        }
      }
    }
  }

  return fragments;
}

template <typename LoggerPolicy>
void fits_categorizer_<LoggerPolicy>::set_metadata_requirements(
    std::string_view category_name, std::string_view requirements) {
  if (!requirements.empty()) {
    auto req = nlohmann::json::parse(requirements);
    if (category_name == IMAGE_CATEGORY) {
      image_req_.parse(req);
    } else {
      compression_metadata_requirements().parse(req);
    }
  }
}

template <typename LoggerPolicy>
bool fits_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category a, fragment_category b) const {
  return meta_.rlock()->less(a.subcategory(), b.subcategory());
}

class fits_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "fits"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/,
         std::shared_ptr<file_access const> const& /*fa*/) const override {
    return make_unique_logging_object<categorizer, fits_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(fits_categorizer_factory)

} // namespace dwarfs::writer
