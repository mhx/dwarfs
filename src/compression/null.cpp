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

#include <cassert>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/option_map.h>

#include "base.h"

namespace dwarfs {

namespace {

class null_block_compressor final : public block_compressor::impl {
 public:
  null_block_compressor() = default;
  null_block_compressor(null_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<null_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    return data;
  }

  compression_type type() const override { return compression_type::NONE; }

  std::string describe() const override { return "null"; }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }
};

class null_block_decompressor final : public block_decompressor_base {
 public:
  null_block_decompressor(std::span<uint8_t const> data)
      : data_(data) {}

  compression_type type() const override { return compression_type::NONE; }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (decompressed_.size() + frame_size > data_.size()) {
      frame_size = data_.size() - decompressed_.size();
    }

    assert(frame_size > 0);

    size_t offset = decompressed_.size();
    decompressed_.resize(offset + frame_size);

    std::copy(data_.data() + offset, data_.data() + offset + frame_size,
              decompressed_.data() + offset);

    return decompressed_.size() == data_.size();
  }

  size_t uncompressed_size() const override { return data_.size(); }

 private:
  std::span<uint8_t const> data_;
};

class null_compression_factory : public compression_factory {
 public:
  static constexpr compression_type type{compression_type::NONE};

  std::string_view name() const override { return "null"; }

  std::string_view description() const override {
    return "no compression at all";
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::set<std::string> library_dependencies() const override { return {}; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map&) const override {
    return std::make_unique<null_block_compressor>();
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data) const override {
    return std::make_unique<null_block_decompressor>(data);
  }

 private:
  std::vector<std::string> const options_{};
};

} // namespace

REGISTER_COMPRESSION_FACTORY(null_compression_factory)

} // namespace dwarfs
