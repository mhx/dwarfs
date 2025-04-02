/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
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
