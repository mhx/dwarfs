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

#include <algorithm>
#include <memory>
#include <vector>

#include "dwarfs/file_interface.h"
#include "dwarfs/file_vector.h"

namespace dwarfs {

namespace detail {

template <class T>
class file_vector_ : public file_vector {
 public:
  file_vector_(std::vector<std::shared_ptr<T>>& vec)
      : vec_(vec) {}

  const file_interface* operator[](size_t i) const override {
    return vec_[i].get();
  }

  size_t size() const override { return vec_.size(); }

  void
  sort(std::function<bool(const file_interface*, const file_interface*)> const&
           less) override {
    std::sort(vec_.begin(), vec_.end(),
              [&](const std::shared_ptr<T>& a, const std::shared_ptr<T>& b) {
                return less(a.get(), b.get());
              });
  }

 private:
  std::vector<std::shared_ptr<T>>& vec_;
};
} // namespace detail

class script {
 public:
  virtual ~script() = default;

  virtual bool filter(file_interface const& fi) const = 0;
  virtual void order(file_vector& fvi) const = 0;

  template <typename T>
  void order(std::vector<std::shared_ptr<T>>& vec) const {
    detail::file_vector_<T> fv(vec);
    order(fv);
  }
};
} // namespace dwarfs
