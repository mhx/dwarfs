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

#include <type_traits>

#include <dwarfs/thrift_lite/internal/concepts.h>
#include <dwarfs/thrift_lite/internal/protocol_type.h>
#include <dwarfs/thrift_lite/internal/type_class.h>

#include <dwarfs/thrift_lite/protocol_reader.h>
#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite::internal {

template <typename TypeClass, typename T>
struct protocol_methods;

template <>
struct protocol_methods<type_class::integral, bool> {
  static void read(protocol_reader& r, bool& out) { out = r.read_bool(); }
  static void write(protocol_writer& w, bool v) { w.write_bool(v); }
};

template <>
struct protocol_methods<type_class::integral, std::int8_t> {
  static void read(protocol_reader& r, std::int8_t& out) {
    out = r.read_byte();
  }
  static void write(protocol_writer& w, std::int8_t v) { w.write_byte(v); }
};

template <>
struct protocol_methods<type_class::integral, std::uint8_t> {
  static void read(protocol_reader& r, std::uint8_t& out) {
    out = static_cast<std::uint8_t>(r.read_byte());
  }
  static void write(protocol_writer& w, std::uint8_t v) {
    w.write_byte(static_cast<std::int8_t>(v));
  }
};

template <>
struct protocol_methods<type_class::integral, std::int16_t> {
  static void read(protocol_reader& r, std::int16_t& out) {
    out = r.read_i16();
  }
  static void write(protocol_writer& w, std::int16_t v) { w.write_i16(v); }
};

template <>
struct protocol_methods<type_class::integral, std::uint16_t> {
  static void read(protocol_reader& r, std::uint16_t& out) {
    out = static_cast<std::uint16_t>(r.read_i16());
  }
  static void write(protocol_writer& w, std::uint16_t v) {
    w.write_i16(static_cast<std::int16_t>(v));
  }
};

template <>
struct protocol_methods<type_class::integral, std::int32_t> {
  static void read(protocol_reader& r, std::int32_t& out) {
    out = r.read_i32();
  }
  static void write(protocol_writer& w, std::int32_t v) { w.write_i32(v); }
};

template <>
struct protocol_methods<type_class::integral, std::uint32_t> {
  static void read(protocol_reader& r, std::uint32_t& out) {
    out = static_cast<std::uint32_t>(r.read_i32());
  }
  static void write(protocol_writer& w, std::uint32_t v) {
    w.write_i32(static_cast<std::int32_t>(v));
  }
};

template <>
struct protocol_methods<type_class::integral, std::int64_t> {
  static void read(protocol_reader& r, std::int64_t& out) {
    out = r.read_i64();
  }
  static void write(protocol_writer& w, std::int64_t v) { w.write_i64(v); }
};

template <>
struct protocol_methods<type_class::integral, std::uint64_t> {
  static void read(protocol_reader& r, std::uint64_t& out) {
    out = static_cast<std::uint64_t>(r.read_i64());
  }
  static void write(protocol_writer& w, std::uint64_t v) {
    w.write_i64(static_cast<std::int64_t>(v));
  }
};

template <enumeration_type T>
struct protocol_methods<type_class::enumeration, T> {
  static void read(protocol_reader& r, T& out) {
    out = static_cast<T>(r.read_i32());
  }
  static void write(protocol_writer& w, T v) {
    w.write_i32(static_cast<std::int32_t>(v));
  }
};

template <typename T>
struct protocol_methods<type_class::binary, T> {
  static void read(protocol_reader& r, T& out) { r.read_binary(out); }
  static void write(protocol_writer& w, T const& v) { w.write_binary(v); }
};

template <typename T>
struct protocol_methods<type_class::string, T> {
  static void read(protocol_reader& r, T& out) { r.read_string(out); }
  static void write(protocol_writer& w, T const& v) { w.write_string(v); }
};

template <typename T>
struct protocol_methods<type_class::structure, T> {
  static void read(protocol_reader& r, T& out) { out.read(r); }
  static void write(protocol_writer& w, T const& v) { v.write(w); }
};

template <typename ValueTypeClass, typename T>
struct protocol_methods<type_class::list<ValueTypeClass>, T> {
  using value_type = typename T::value_type;
  using value_protocol_methods = protocol_methods<ValueTypeClass, value_type>;

  static void read(protocol_reader& r, T& out) {
    auto elem_type = ttype{};
    auto size = std::int32_t{};

    r.read_list_begin(elem_type, size);

    if (elem_type != ttype_v<ValueTypeClass, value_type>) {
      for (auto i = std::int32_t{0}; i < size; ++i) {
        r.skip(elem_type);
      }
    } else {
      out.clear();
      for (auto i = std::int32_t{0}; i < size; ++i) {
        value_protocol_methods::read(r, out.emplace_back());
      }
    }

    r.read_list_end();
  }

  static void write(protocol_writer& w, T const& v) {
    w.write_list_begin(ttype_v<ValueTypeClass, value_type>,
                       static_cast<std::int32_t>(v.size()));
    for (auto const& elem : v) {
      value_protocol_methods::write(w, elem);
    }
    w.write_list_end();
  }
};

template <typename ValueTypeClass, typename T>
struct protocol_methods<type_class::set<ValueTypeClass>, T> {
  using value_type = typename T::value_type;
  using value_protocol_methods = protocol_methods<ValueTypeClass, value_type>;

  static void read(protocol_reader& r, T& out) {
    auto elem_type = ttype{};
    auto size = std::int32_t{};

    r.read_set_begin(elem_type, size);

    if (elem_type != ttype_v<ValueTypeClass, value_type>) {
      for (auto i = std::int32_t{0}; i < size; ++i) {
        r.skip(elem_type);
      }
    } else {
      out.clear();
      for (auto i = std::int32_t{0}; i < size; ++i) {
        value_type tmp;
        value_protocol_methods::read(r, tmp);
        out.insert(std::move(tmp));
      }
    }

    r.read_set_end();
  }

  static void write(protocol_writer& w, T const& v) {
    w.write_set_begin(ttype_v<ValueTypeClass, value_type>,
                      static_cast<std::int32_t>(v.size()));
    for (auto const& elem : v) {
      value_protocol_methods::write(w, elem);
    }
    w.write_set_end();
  }
};

template <typename KeyTypeClass, typename MappedTypeClass, typename T>
struct protocol_methods<type_class::map<KeyTypeClass, MappedTypeClass>, T> {
  using key_type = typename T::key_type;
  using key_protocol_methods = protocol_methods<KeyTypeClass, key_type>;
  using mapped_type = typename T::mapped_type;
  using mapped_protocol_methods =
      protocol_methods<MappedTypeClass, mapped_type>;

  static void read(protocol_reader& r, T& out) {
    auto key_tt = ttype{};
    auto mapped_tt = ttype{};
    auto size = std::int32_t{};

    r.read_map_begin(key_tt, mapped_tt, size);

    if (key_tt != ttype_v<KeyTypeClass, key_type> ||
        mapped_tt != ttype_v<MappedTypeClass, mapped_type>) {
      for (auto i = std::int32_t{0}; i < size; ++i) {
        r.skip(key_tt);
        r.skip(mapped_tt);
      }
    } else {
      out.clear();
      for (auto i = std::int32_t{0}; i < size; ++i) {
        key_type tmp_key;
        key_protocol_methods::read(r, tmp_key);
        mapped_type tmp_mapped;
        mapped_protocol_methods::read(r, tmp_mapped);
        out.emplace(std::move(tmp_key), std::move(tmp_mapped));
      }
    }

    r.read_map_end();
  }

  static void write(protocol_writer& w, T const& v) {
    w.write_map_begin(ttype_v<KeyTypeClass, key_type>,
                      ttype_v<MappedTypeClass, mapped_type>,
                      static_cast<std::int32_t>(v.size()));
    for (auto const& [key, mapped] : v) {
      key_protocol_methods::write(w, key);
      mapped_protocol_methods::write(w, mapped);
    }
    w.write_map_end();
  }
};

} // namespace dwarfs::thrift_lite::internal
