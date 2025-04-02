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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <dwarfs/file_stat.h>

namespace dwarfs::writer::internal {

class chmod_transformer {
 public:
  using mode_type = file_stat::mode_type;

  chmod_transformer(std::string_view spec, mode_type umask);

  std::optional<mode_type> transform(mode_type mode, bool isdir) const {
    return impl_->transform(mode, isdir);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::optional<mode_type>
    transform(mode_type mode, bool isdir) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::writer::internal
