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

#include <sstream>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <dwarfs/compression.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/sorted_array_map.h>

namespace dwarfs {

namespace {

using namespace std::string_view_literals;

// clang-format off
constexpr sorted_array_map sections{
#define SECTION_TYPE_(x) std::pair{section_type::x, #x ## sv}
    SECTION_TYPE_(BLOCK),
    SECTION_TYPE_(METADATA_V2_SCHEMA),
    SECTION_TYPE_(METADATA_V2),
    SECTION_TYPE_(SECTION_INDEX),
    SECTION_TYPE_(HISTORY),
#undef SECTION_TYPE_
};

constexpr sorted_array_map compressions {
#define DWARFS_COMPRESSION_TYPE_(name, _) std::pair{compression_type::name, #name ## sv}
#define DWARFS_COMMA_ ,
  DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE_, DWARFS_COMMA_)
#undef DWARFS_COMPRESSION_TYPE_
#undef DWARFS_COMMA_
};
// clang-format on

std::string get_default(auto const& map, auto key) {
  if (auto value = map.get(key)) {
    return std::string{*value};
  }
  return fmt::format("unknown ({})", static_cast<int>(key));
}

} // namespace

bool is_known_compression_type(compression_type type) {
  return compressions.contains(type);
}

bool is_known_section_type(section_type type) {
  return sections.contains(type);
}

std::string get_compression_name(compression_type type) {
  return get_default(compressions, type);
}

std::string get_section_name(section_type type) {
  return get_default(sections, type);
}

void section_header::dump(std::ostream& os) const {
  os << "[V1] type=" << get_default(sections, type) << ", compression="
     << get_compression_name(static_cast<compression_type>(compression))
     << ", length=" << length;
}

std::string section_header::to_string() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

void section_header_v2::dump(std::ostream& os) const {
  os << "[V" << static_cast<int>(major) << "." << static_cast<int>(minor)
     << "] num=" << number
     << ", type=" << get_default(sections, static_cast<section_type>(type))
     << ", compression="
     << get_compression_name(static_cast<compression_type>(compression))
     << ", length=" << length
     << ", checksum=" << fmt::format("{:#018x}", xxh3_64);
}

std::string section_header_v2::to_string() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

} // namespace dwarfs
