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

#pragma once

#include <vector>

#include <folly/Range.h>

#include "fstypes.h"
#include "logger.h"

namespace dwarfs {

class metadata_writer {
 public:
  using const_iterator = std::vector<uint8_t>::const_iterator;

  metadata_writer(logger& lgr, std::vector<uint8_t>& mem,
                  size_t section_align = 8);
  void align(size_t align);
  void finish_section();
  void start_section(section_type type);
  uint8_t* buffer(size_t size);
  void write(const void* data, size_t size);

  const_iterator begin() const { return mem_.begin(); }

  const_iterator section_begin() const {
    return mem_.begin() + section_data_offset();
  }

  const uint8_t* section_data() const {
    return mem_.data() + section_data_offset();
  }

  size_t section_data_size() const {
    return mem_.size() - section_data_offset();
  }

  size_t section_data_offset() const {
    return section_header_offset_ + sizeof(section_header);
  }

  const_iterator end() const { return mem_.end(); }

  size_t offset() const { return mem_.size(); }

  template <typename T>
  void write(const T& obj) {
    write(&obj, sizeof(T));
  }

  template <typename T>
  void write(const std::vector<T>& vec) {
    if (!vec.empty()) {
      write(vec.data(), sizeof(T) * vec.size());
    }
  }

  void write(const std::string& str) {
    if (!str.empty()) {
      write(str.data(), str.size());
    }
  }

  void write(folly::StringPiece str) {
    if (!str.empty()) {
      write(str.data(), str.size());
    }
  }

 private:
  std::vector<uint8_t>& mem_;
  size_t section_header_offset_;
  const size_t section_align_;
  log_proxy<debug_logger_policy> log_;
};
} // namespace dwarfs
