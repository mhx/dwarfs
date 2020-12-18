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

#include "dwarfs/mmif.h"

namespace dwarfs {
namespace test {

class mmap_mock : public mmif {
 public:
  mmap_mock(const std::string& data)
      : m_data(data) {}

  void const* addr() const override { return m_data.data(); }

  size_t size() const override { return m_data.size(); }

  boost::system::error_code lock(off_t, size_t) override {
    return boost::system::error_code();
  }
  boost::system::error_code release(off_t, size_t) override {
    return boost::system::error_code();
  }

 private:
  const std::string m_data;
};

} // namespace test
} // namespace dwarfs
