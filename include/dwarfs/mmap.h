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

#include <cstddef>
#include <string>

#include "dwarfs/mmif.h"

namespace dwarfs {

class mmap : public mmif {
 public:
  explicit mmap(const std::string& path);
  mmap(const std::string& path, size_t size);

  ~mmap() noexcept override;

  void const* addr() const override;
  size_t size() const override;

  std::error_code lock(off_t offset, size_t size) override;
  std::error_code release(off_t offset, size_t size) override;
  std::error_code release_until(off_t offset) override;

 private:
  int fd_;
  size_t size_;
  void* addr_;
  off_t const page_size_;
};
} // namespace dwarfs
