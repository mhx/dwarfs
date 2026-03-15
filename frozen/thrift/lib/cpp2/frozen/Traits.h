/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#pragma once
#include <type_traits>

namespace apache::thrift {

template <class>
struct IsString : std::false_type {};
template <class>
struct IsHashMap : std::false_type {};
template <class>
struct IsHashSet : std::false_type {};
template <class>
struct IsOrderedMap : std::false_type {};
template <class>
struct IsOrderedSet : std::false_type {};
template <class>
struct IsList : std::false_type {};

namespace frozen {
template <class>
struct IsExcluded : std::false_type {};
} // namespace frozen

} // namespace apache::thrift

#define THRIFT_DECLARE_TRAIT(Trait, ...)         \
  namespace apache {                             \
  namespace thrift {                             \
  template <>                                    \
  struct Trait<__VA_ARGS__> : std::true_type {}; \
  }                                              \
  }

#define THRIFT_DECLARE_TRAIT_TEMPLATE(Trait, ...)         \
  namespace apache {                                      \
  namespace thrift {                                      \
  template <class... Args>                                \
  struct Trait<__VA_ARGS__<Args...>> : std::true_type {}; \
  }                                                       \
  }
