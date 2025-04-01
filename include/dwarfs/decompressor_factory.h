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

#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <dwarfs/block_decompressor.h>

namespace dwarfs {

class decompressor_info {
 public:
  virtual ~decompressor_info() = default;

  virtual std::string_view name() const = 0;
  virtual std::string_view description() const = 0;
  virtual std::set<std::string> library_dependencies() const = 0;
};

class decompressor_factory : public decompressor_info {
 public:
  virtual std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const = 0;
};

} // namespace dwarfs
