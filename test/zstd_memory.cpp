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

#include <algorithm>
#include <iostream>
#include <string_view>

#include <fmt/format.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/bits.h>
#include <dwarfs/block_compressor.h>
#include <dwarfs/util.h>

using namespace dwarfs;
using namespace dwarfs::binary_literals;
using namespace std::string_view_literals;

int main() {
  // Options are going to be set as follows if we don't have a proper,
  // statically linked zstd library:
  // - `level` can be chosen by the user
  // - `long` can be specified and will automatically set `window_log`
  //   according to the data (block) size
  //
  // If we have a proper zstd library, the above will happen just as
  // before, but we will then allow applying any other option overrides.

  using compressed_usage_type = uint16_t;
  static constexpr auto compressed_usage_type_max =
      std::numeric_limits<compressed_usage_type>::max();

  for (bool const enable_long : {false, true}) {
    std::array<uint64_t, 23 * 21> usage{};
    uint64_t min_mem = UINT64_MAX;
    uint64_t max_mem = 0;

    for (auto level = 0; level <= 22; ++level) {
      auto options = fmt::format("zstd:level={}", level);
      if (enable_long) {
        options += ":long";
      }
      auto bc = block_compressor(options);

      for (auto data_size = 1_KiB; data_size <= 1_GiB; data_size *= 2) {
        auto const mem = bc.estimate_memory_usage(data_size) - data_size;
        usage[level * 21 + (log2_bit_ceil(data_size) - 10)] = mem;
        min_mem = std::min(min_mem, mem);
        max_mem = std::max(max_mem, mem);
      }
    }

    uint64_t factor = min_mem;
    double base = std::exp2(
        (std::log(static_cast<double>(max_mem) / min_mem) / std::log(2)) /
        compressed_usage_type_max);
    auto const suffix = enable_long ? "Long"sv : ""sv;

    std::cout << fmt::format("constexpr double kZstdMemUsageFactor{} = {};\n",
                             suffix, factor);
    std::cout << fmt::format("constexpr double kZstdMemUsageBase{} = {};\n",
                             suffix, base);
    std::cout << fmt::format(
        "constexpr std::array<uint16_t, 23*21> kZstdMemUsage{}{{\n", suffix);

    std::array<compressed_usage_type, 23 * 21> usage_scaled{};
    std::ranges::transform(
        usage, usage_scaled.begin(),
        [factor, q = std::log(base)](uint64_t mem) {
          return static_cast<compressed_usage_type>(std::clamp<int>(
              std::ceil(std::log(static_cast<double>(mem) / factor) / q), 0,
              compressed_usage_type_max));
        });

    for (size_t i = 0; i < usage_scaled.size(); ++i) {
      std::cout << static_cast<int>(usage_scaled[i]);
      if ((i + 1) % 21 == 0) {
        std::cout << ",\n";
      } else {
        std::cout << ", ";
      }
    }

    std::cout << "};\n\n";

    for (size_t i = 0; i < usage_scaled.size(); ++i) {
      auto const mem_ref = usage[i];
      auto const mem = static_cast<uint64_t>(
          std::ceil(factor * std::pow(base, usage_scaled[i])));
      auto const deviation = static_cast<double>(mem) / mem_ref - 1.0;
      if (mem < mem_ref || deviation > 0.0002) {
        std::cerr << mem << " (ref: " << mem_ref
                  << ", scaled: " << static_cast<int>(usage_scaled[i])
                  << ", deviation: " << deviation * 100 << "%)\n";
      }
    }
  }

  return 0;
}
