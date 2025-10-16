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

#include <cassert>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/util.h>

#include <dwarfs/writer/internal/time_resolution_converter.h>

namespace dwarfs::writer::internal {

namespace {

uint32_t
get_sec_multiplier(std::optional<std::chrono::nanoseconds> resolution) {
  if (resolution.has_value()) {
    if (auto const res = *resolution; res > std::chrono::seconds{1}) {
      if (res % std::chrono::seconds{1} != std::chrono::nanoseconds{0}) {
        DWARFS_THROW(
            runtime_error,
            fmt::format("cannot handle resolution ({}) that is larger than one "
                        "second but not a whole number of seconds",
                        time_with_unit(res)));
      }

      return static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::seconds>(res).count());
    }
  }

  return 1;
}

uint32_t
get_nsec_multiplier(std::optional<std::chrono::nanoseconds> resolution) {
  if (resolution.has_value()) {
    if (auto const res = *resolution; res < std::chrono::seconds{1}) {
      if (std::chrono::seconds{1} % res != std::chrono::nanoseconds{0}) {
        DWARFS_THROW(runtime_error,
                     fmt::format("cannot handle subsecond resolution ({}) that "
                                 "is not a whole divisor of one second",
                                 time_with_unit(res)));
      }

      return static_cast<uint32_t>(res.count());
    }
  }

  return 0;
}

std::chrono::nanoseconds get_chrono_resolution(uint32_t sec, uint32_t nsec) {
  std::chrono::nanoseconds rv;

  if (nsec > 0) {
    rv = std::chrono::nanoseconds{nsec};
    assert(sec == 1);
    assert(rv < std::chrono::seconds{1});
  } else {
    rv = std::chrono::seconds{sec};
    assert(nsec == 0);
    assert(rv >= std::chrono::seconds{1});
  }

  return rv;
}

} // namespace

struct time_resolution_converter::init_data {
  explicit init_data(std::optional<std::chrono::nanoseconds> resolution)
      : init_data(resolution.value_or(std::chrono::seconds{1}),
                  {.sec = 1, .nsec = 1}) {}

  init_data(std::optional<std::chrono::nanoseconds> resolution,
            time_conversion_factors const& old_conv) {
    auto old_sec = old_conv.sec.value_or(1);
    auto old_nsec = old_conv.nsec.value_or(0);
    auto new_sec =
        resolution.has_value() ? get_sec_multiplier(resolution) : old_sec;
    auto new_nsec =
        resolution.has_value() ? get_nsec_multiplier(resolution) : old_nsec;

    auto const old_resolution = get_chrono_resolution(old_sec, old_nsec);
    auto const new_resolution = get_chrono_resolution(new_sec, new_nsec);

    if (new_resolution < old_resolution) {
      DWARFS_THROW(runtime_error,
                   fmt::format("cannot convert time to a finer resolution ({}) "
                               "than the old resolution ({})",
                               time_with_unit(new_resolution),
                               time_with_unit(old_resolution)));
    }

    if (new_resolution % old_resolution > std::chrono::nanoseconds{0}) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("cannot convert time to a coarser resolution ({}) that "
                      "is not a whole multiple of the old resolution ({})",
                      time_with_unit(new_resolution),
                      time_with_unit(old_resolution)));
    }

    assert(new_sec > 0);
    assert(old_sec > 0);
    assert(new_sec % old_sec == 0);
    assert((old_nsec == 0 && new_nsec == 0) || new_nsec % old_nsec == 0);

    conv_sec = new_sec / old_sec;
    conv_subsec = old_nsec > 0 ? new_nsec / old_nsec : 1;

    if (new_sec > 1) {
      new_conv.sec = new_sec;
    }

    if (new_nsec > 0) {
      new_conv.nsec = new_nsec;
    }
  }

  uint32_t conv_sec{1};
  uint32_t conv_subsec{1};
  time_conversion_factors new_conv;
};

time_resolution_converter::time_resolution_converter(
    std::optional<std::chrono::nanoseconds> resolution)
    : time_resolution_converter{init_data{resolution}} {}

time_resolution_converter::time_resolution_converter(
    std::optional<std::chrono::nanoseconds> resolution,
    time_conversion_factors const& old_conv)
    : time_resolution_converter{init_data{resolution, old_conv}} {}

time_resolution_converter::time_resolution_converter(init_data const& data)
    : conv_sec_{data.conv_sec}
    , conv_subsec_{data.conv_subsec}
    , new_conv_{data.new_conv} {}

bool time_resolution_converter::requires_conversion() const {
  return conv_sec_ != 1 || conv_subsec_ != 1;
}

} // namespace dwarfs::writer::internal
