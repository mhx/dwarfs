/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// IWYU pragma: private, include "thrift/lib/cpp2/frozen/Frozen.h"

namespace apache {
namespace thrift {
namespace frozen {
namespace detail {

/**
 * Layout specialization for boolean values. Stores a bool in 0 or 1 bits. 0
 * bits may only be used if the value is always false.
 */
struct BoolLayout : public LayoutBase {
  typedef LayoutBase Base;
  typedef bool T;

  BoolLayout() : LayoutBase(typeid(T)) {}

  FieldPosition maximize() {
    FieldPosition pos = startFieldPosition();
    ++pos.bitOffset;
    return pos;
  }

  FieldPosition layout(LayoutRoot&, const T& o, LayoutPosition /* self */) {
    FieldPosition pos = startFieldPosition();
    if (o) {
      ++pos.bitOffset;
    }
    return pos;
  }

  void freeze(FreezeRoot&, const T& o, FreezePosition self) const {
    if (bits) {
      if (o) {
        dwarfs::bit_view(self.start).set(self.bitOffset);
      } else {
        dwarfs::bit_view(self.start).clear(self.bitOffset);
      }
    }
  }

  void thaw(ViewPosition self, T& out) const {
    if (bits) {
      out = dwarfs::bit_view(self.start).test(self.bitOffset);
    } else {
      out = false;
    }
  }

  void print(std::ostream& os, int level) const override {
    LayoutBase::print(os, level);
    os << "packed bool";
  }

  typedef T View;

  View view(ViewPosition self) const {
    View v;
    thaw(self, v);
    return v;
  }
};
} // namespace detail

template <>
struct Layout<bool, void> : public apache::thrift::frozen::detail::BoolLayout {
};

} // namespace frozen
} // namespace thrift
} // namespace apache
