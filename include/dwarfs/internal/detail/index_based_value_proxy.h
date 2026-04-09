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

#pragma once

namespace dwarfs::internal::detail {

template <typename Container>
class index_based_value_proxy {
 public:
  using container_type = Container;
  using value_type = typename container_type::value_type;
  using size_type = typename container_type::size_type;

  index_based_value_proxy(container_type& vec, size_type i)
      : vec_{vec}
      , i_{i} {}

  operator value_type() const { return vec_.get(i_); }

  index_based_value_proxy const& operator=(value_type value) const {
    vec_.set(i_, value);
    return *this;
  }

  index_based_value_proxy const&
  operator=(index_based_value_proxy const& other) const {
    return *this = static_cast<value_type>(other);
  }

  friend void swap(index_based_value_proxy a, index_based_value_proxy b) {
    value_type tmp = a;
    a = static_cast<value_type>(b);
    b = tmp;
  }

 private:
  container_type& vec_;
  size_type i_;
};

} // namespace dwarfs::internal::detail
