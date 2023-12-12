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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dwarfs/compression.h"
#include "dwarfs/compression_constraints.h"

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

  explicit block_compressor(const std::string& spec);

  block_compressor(const block_compressor& bc)
      : impl_(bc.impl_->clone()) {}

  block_compressor(block_compressor&& bc) = default;
  block_compressor& operator=(block_compressor&& rhs) = default;

  std::vector<uint8_t> compress(std::vector<uint8_t> const& data) const {
    return impl_->compress(data, nullptr);
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const {
    return impl_->compress(std::move(data), nullptr);
  }

  std::vector<uint8_t> compress(std::vector<uint8_t> const& data,
                                std::string const& metadata) const {
    return impl_->compress(data, &metadata);
  }

  std::vector<uint8_t>
  compress(std::vector<uint8_t>&& data, std::string const& metadata) const {
    return impl_->compress(std::move(data), &metadata);
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

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::unique_ptr<impl> clone() const = 0;

    virtual std::vector<uint8_t>
    compress(const std::vector<uint8_t>& data,
             std::string const* metadata) const = 0;
    virtual std::vector<uint8_t>
    compress(std::vector<uint8_t>&& data,
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
  block_decompressor(compression_type type, const uint8_t* data, size_t size,
                     std::vector<uint8_t>& target);

  bool decompress_frame(size_t frame_size = BUFSIZ) {
    return impl_->decompress_frame(frame_size);
  }

  size_t uncompressed_size() const { return impl_->uncompressed_size(); }

  compression_type type() const { return impl_->type(); }

  std::optional<std::string> metadata() const { return impl_->metadata(); }

  static std::vector<uint8_t>
  decompress(compression_type type, const uint8_t* data, size_t size) {
    std::vector<uint8_t> target;
    block_decompressor bd(type, data, size, target);
    bd.decompress_frame(bd.uncompressed_size());
    return target;
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
  virtual std::vector<std::string> const& options() const = 0;
};

class compression_factory : public compression_info {
 public:
  virtual std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const = 0;
  virtual std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const = 0;
};

namespace detail {

template <typename T>
class compression_factory_registrar {
 public:
  compression_factory_registrar(compression_type type);
};

} // namespace detail

class compression_registry {
 public:
  template <typename T>
  friend class detail::compression_factory_registrar;

  static compression_registry& instance();

  std::unique_ptr<block_compressor::impl>
  make_compressor(std::string_view spec) const;
  std::unique_ptr<block_decompressor::impl>
  make_decompressor(compression_type type, std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const;

  void for_each_algorithm(
      std::function<void(compression_type, compression_info const&)> const& fn)
      const;

 private:
  compression_registry();
  ~compression_registry();

  void register_factory(compression_type type,
                        std::unique_ptr<compression_factory const>&& factory);

  std::unordered_map<compression_type,
                     std::unique_ptr<compression_factory const>>
      factories_;
  std::unordered_map<std::string, compression_type> names_;
};

namespace detail {

template <typename T>
compression_factory_registrar<T>::compression_factory_registrar(
    compression_type type) {
  ::dwarfs::compression_registry::instance().register_factory(
      type, std::make_unique<T>());
}

} // namespace detail

#define REGISTER_COMPRESSION_FACTORY(type, factory)                            \
  namespace {                                                                  \
  ::dwarfs::detail::compression_factory_registrar<factory>                     \
      the_##factory##_registrar(type);                                         \
  }

} // namespace dwarfs
