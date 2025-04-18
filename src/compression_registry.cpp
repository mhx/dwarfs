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

#include <cstdlib>
#include <iostream>
#include <string>

#include <dwarfs/detail/compression_registry.h>
#include <dwarfs/error.h>

namespace dwarfs::detail {

void compression_registry_base::register_name(compression_type type,
                                              std::string_view name) {
  if (!names_.emplace(std::string{name}, type).second) {
    std::cerr << "compression factory name conflict (" << name << ", "
              << static_cast<int>(type) << ")\n";
    ::abort();
  }
}

compression_type
compression_registry_base::get_type(std::string const& name) const {
  auto nit = names_.find(name);

  if (nit == names_.end()) {
    DWARFS_THROW(runtime_error, "unknown compression: " + name);
  }

  return nit->second;
}

} // namespace dwarfs::detail
