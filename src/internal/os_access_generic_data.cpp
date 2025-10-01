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

#include <fmt/format.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/option_parser.h>
#include <dwarfs/internal/os_access_generic_data.h>

using namespace dwarfs::binary_literals;

namespace dwarfs::internal {

namespace {

constexpr bool kIs32BitArch = sizeof(void*) == 4;
constexpr auto kIolayerOptsVar = "DWARFS_IOLAYER_OPTS";
constexpr auto kMaxEagerMapSizeOpt = "max_eager_map_size";

} // namespace

os_access_generic_data::os_access_generic_data(std::ostream& err,
                                               get_env_func const& get_env)
    : mm_ops_{get_native_memory_mapping_ops()} {
  if (kIs32BitArch) {
    fv_opts_.max_eager_map_size.emplace(32_MiB);
  }

  if (auto const value = get_env(kIolayerOptsVar)) {
    option_parser parser{value};

    if (auto const max_eager = parser.get(kMaxEagerMapSizeOpt)) {
      if (*max_eager == "unlimited") {
        fv_opts_.max_eager_map_size.reset();
      } else {
        try {
          fv_opts_.max_eager_map_size.emplace(
              parse_size_with_unit(std::string{*max_eager}));
        } catch (std::exception const& e) {
          err << fmt::format("warning: ignoring invalid {} option '{}': {}\n",
                             kIolayerOptsVar, kMaxEagerMapSizeOpt,
                             exception_str(e));
        }
      }
    }

    parser.report_unused(
        [&err](std::string_view key, std::string_view /*value*/) {
          err << fmt::format("warning: ignoring unknown {} option '{}'\n",
                             kIolayerOptsVar, key);
        });
  }
}

} // namespace dwarfs::internal
