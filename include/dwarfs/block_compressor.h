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

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dwarfs/compression.h>
#include <dwarfs/compression_constraints.h>
#include <dwarfs/vector_byte_buffer.h>

namespace dwarfs {

class option_map;

class bad_compression_ratio_error : public std::runtime_error {
 public:
  bad_compression_ratio_error()
      : std::runtime_error{"bad compression ratio"} {}
};

class block_compressor {
 public:
  block_compressor() = default;

  explicit block_compressor(std::string const& spec);

  block_compressor(block_compressor const& bc)
      : impl_(bc.impl_->clone()) {}

  block_compressor(block_compressor&& bc) = default;
  block_compressor& operator=(block_compressor&& rhs) = default;

  shared_byte_buffer compress(shared_byte_buffer const& data) const {
    return impl_->compress(data, nullptr);
  }

  shared_byte_buffer
  compress(shared_byte_buffer const& data, std::string const& metadata) const {
    return impl_->compress(data, &metadata);
  }

  compression_type type() const { return impl_->type(); }

  std::string describe() const { return impl_->describe(); }

  std::string metadata_requirements() const {
    return impl_->metadata_requirements();
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const {
    return impl_->get_compression_constraints(metadata);
  }

  explicit operator bool() const { return static_cast<bool>(impl_); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::unique_ptr<impl> clone() const = 0;

    virtual shared_byte_buffer compress(shared_byte_buffer const& data,
                                        std::string const* metadata) const = 0;

    virtual compression_type type() const = 0;
    virtual std::string describe() const = 0;

    virtual std::string metadata_requirements() const = 0;

    virtual compression_constraints
    get_compression_constraints(std::string const& metadata) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class block_decompressor {
 public:
  block_decompressor(compression_type type, std::span<uint8_t const> data,
                     mutable_byte_buffer target);

  bool decompress_frame(size_t frame_size = BUFSIZ) {
    return impl_->decompress_frame(frame_size);
  }

  size_t uncompressed_size() const { return impl_->uncompressed_size(); }

  compression_type type() const { return impl_->type(); }

  std::optional<std::string> metadata() const { return impl_->metadata(); }

  static shared_byte_buffer
  decompress(compression_type type, std::span<uint8_t const> data) {
    auto target = vector_byte_buffer::create();
    block_decompressor bd(type, data, target);
    bd.decompress_frame(bd.uncompressed_size());
    return target.share();
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual bool decompress_frame(size_t frame_size) = 0;
    virtual size_t uncompressed_size() const = 0;
    virtual std::optional<std::string> metadata() const = 0;

    virtual compression_type type() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class compression_info {
 public:
  virtual ~compression_info() = default;

  virtual std::string_view name() const = 0;
  virtual std::string_view description() const = 0;
  virtual std::vector<std::string> const& options() const = 0; // TODO: span?
  virtual std::set<std::string> library_dependencies() const = 0;
};

class compression_factory : public compression_info {
 public:
  virtual std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const = 0;
  virtual std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    mutable_byte_buffer target) const = 0;
};

namespace detail {

template <compression_type T>
struct compression_factory_registrar;

} // namespace detail

class compression_registry {
 public:
  static compression_registry& instance();

  std::unique_ptr<block_compressor::impl>
  make_compressor(std::string_view spec) const;
  std::unique_ptr<block_decompressor::impl>
  make_decompressor(compression_type type, std::span<uint8_t const> data,
                    mutable_byte_buffer target) const;

  void for_each_algorithm(
      std::function<void(compression_type, compression_info const&)> const& fn)
      const;

  void register_factory(compression_type type,
                        std::unique_ptr<compression_factory const>&& factory);

 private:
  compression_registry();
  ~compression_registry();

  std::unordered_map<compression_type,
                     std::unique_ptr<compression_factory const>>
      factories_;
  std::unordered_map<std::string, compression_type> names_;
};

namespace detail {

#define DWARFS_COMPRESSION_TYPE_ENUMERATION_(name, value)                      \
  template <>                                                                  \
  struct compression_factory_registrar<compression_type::name> {               \
    static void reg(compression_registry&);                                    \
  };
#define DWARFS_NO_SEPARATOR_
DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE_ENUMERATION_,
                             DWARFS_NO_SEPARATOR_)
#undef DWARFS_COMPRESSION_TYPE_ENUMERATION_
#undef DWARFS_NO_SEPARATOR_

} // namespace detail

#define REGISTER_COMPRESSION_FACTORY(factory)                                  \
  void ::dwarfs::detail::compression_factory_registrar<factory::type>::reg(    \
      ::dwarfs::compression_registry& cr) {                                    \
    cr.register_factory(factory::type, std::make_unique<factory>());           \
  }

} // namespace dwarfs
