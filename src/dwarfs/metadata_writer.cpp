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

#include <cstring>

#include "dwarfs/metadata_writer.h"

namespace dwarfs {

metadata_writer::metadata_writer(logger& lgr, std::vector<uint8_t>& mem,
                                 size_t section_align)
    : mem_(mem)
    , section_header_offset_(0)
    , section_align_(section_align)
    , log_(lgr) {}

void metadata_writer::align(size_t align) {
  size_t align_off = mem_.size() % align;

  if (align_off > 0) {
    mem_.resize(mem_.size() + (align - align_off));
  }
}

void metadata_writer::finish_section() {
  align(section_align_);
  section_header* p_sh =
      reinterpret_cast<section_header*>(&mem_[section_header_offset_]);
  p_sh->length =
      mem_.size() - (section_header_offset_ + sizeof(section_header));
  log_.debug() << "section size " << p_sh->length;
}

void metadata_writer::start_section(section_type type) {
  log_.debug() << "section @ " << offset();
  section_header_offset_ = offset();
  section_header sh;
  sh.type = type;
  sh.compression = compression_type::NONE;
  sh.unused = 0;
  sh.length = 0;
  write(sh);
}

uint8_t* metadata_writer::buffer(size_t size) {
  size_t offs = offset();
  mem_.resize(offs + size);
  return &mem_[offs];
}

void metadata_writer::write(const void* data, size_t size) {
  if (size > 0) {
    ::memcpy(buffer(size), data, size);
  }
}
} // namespace dwarfs
