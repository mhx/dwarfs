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

#include <algorithm>
#include <vector>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/error.h>
#include <dwarfs/option_map.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

namespace dwarfs {

option_map::option_map(std::string_view const spec) {
  auto arg = split_to<std::vector<std::string_view>>(spec, ':');

  choice_ = arg[0];

  for (size_t i = 1; i < arg.size(); ++i) {
    std::string key;
    std::string val;

    if (auto eqpos = arg[i].find('='); eqpos != std::string_view::npos) {
      key.assign(arg[i].substr(0, eqpos));
      val.assign(arg[i].substr(eqpos + 1));
    } else {
      key.assign(arg[i]);
      val.assign("1");
    }

    if (!opt_.emplace(key, val).second) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("duplicate option {} for choice {}", key, choice_));
    }
  }
}

size_t option_map::get_size(std::string const& key, size_t default_value) {
  auto i = opt_.find(key);

  if (i != opt_.end()) {
    std::string val = i->second;
    opt_.erase(i);
    return parse_size_with_unit(val);
  }

  return default_value;
}

void option_map::report() {
  if (!opt_.empty()) {
    std::vector<std::string> invalid;
    std::ranges::transform(opt_, std::back_inserter(invalid),
                           [](auto const& p) { return p.first; });
    std::ranges::sort(invalid);
    DWARFS_THROW(runtime_error,
                 fmt::format("invalid option(s) for choice {}: {}", choice_,
                             fmt::join(invalid, ", ")));
  }
}

} // namespace dwarfs
