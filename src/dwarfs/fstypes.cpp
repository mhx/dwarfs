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

#include <map>
#include <sstream>
#include <string>

#include <folly/Conv.h>

#include "dwarfs/fstypes.h"

namespace dwarfs {

namespace {

const std::map<section_type, std::string> sections{
#define SECTION_TYPE_(x) {section_type::x, #x}
    SECTION_TYPE_(BLOCK),
    SECTION_TYPE_(METADATA),
    SECTION_TYPE_(META_TABLEDATA),
    SECTION_TYPE_(META_INODE_INDEX),
    SECTION_TYPE_(META_CHUNK_INDEX),
    SECTION_TYPE_(META_DIRECTORIES),
    SECTION_TYPE_(META_CONFIG),
    SECTION_TYPE_(METADATA_V2_SCHEMA),
    SECTION_TYPE_(METADATA_V2),
#undef SECTION_TYPE_
};

// TODO: remove
const std::map<dir_entry_type, std::string> dir_entries{
#define DIR_ENTRY_TYPE_(x) {dir_entry_type::x, #x}
    DIR_ENTRY_TYPE_(DIR_ENTRY), DIR_ENTRY_TYPE_(DIR_ENTRY_UG),
    DIR_ENTRY_TYPE_(DIR_ENTRY_UG_TIME)
#undef DIR_ENTRY_TYPE_
};

const std::map<compression_type, std::string> compressions{
#define COMPRESSION_TYPE_(x) {compression_type::x, #x}
    COMPRESSION_TYPE_(NONE), COMPRESSION_TYPE_(LZMA), COMPRESSION_TYPE_(ZSTD),
    COMPRESSION_TYPE_(LZ4), COMPRESSION_TYPE_(LZ4HC)
#undef COMPRESSION_TYPE_
};

template <typename HT>
typename HT::mapped_type
get_default(const HT& ht, const typename HT::key_type& key) {
  auto i = ht.find(key);

  if (i != ht.end()) {
    return i->second;
  }

  return folly::to<typename HT::mapped_type>("unknown (", key, ")");
}
} // namespace

std::string get_compression_name(compression_type type) {
  return get_default(compressions, type);
}

std::string get_section_name(section_type type) {
  return get_default(sections, type);
}

void section_header::dump(std::ostream& os) const {
  os << "type=" << get_default(sections, type)
     << ", compression=" << get_default(compressions, compression)
     << ", length=" << length;
}

std::string section_header::to_string() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

} // namespace dwarfs
