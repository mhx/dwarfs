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

#include <dwarfs/block_compressor.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/option_map.h>

namespace dwarfs {

namespace {

class null_block_compressor final : public block_compressor::impl {
 public:
  null_block_compressor() = default;
  null_block_compressor(null_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<null_block_compressor>(*this);
  }

  // TODO: we should not have to copy the data here...
  std::vector<uint8_t>
  compress(std::span<uint8_t const> data,
           std::string const* /*metadata*/) const override {
    return std::vector<uint8_t>(data.begin(), data.end());
  }

  compression_type type() const override { return compression_type::NONE; }

  std::string describe() const override { return "null"; }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }
};

class null_block_decompressor final : public block_decompressor::impl {
 public:
  null_block_decompressor(uint8_t const* data, size_t size,
                          mutable_byte_buffer target)
      : decompressed_(target)
      , data_(data)
      , uncompressed_size_(size) {
    // TODO: we shouldn't have to copy this to memory at all...
    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("could not reserve {} bytes for decompressed block",
                      uncompressed_size_));
    }
  }

  compression_type type() const override { return compression_type::NONE; }

  std::optional<std::string> metadata() const override { return std::nullopt; }

  bool decompress_frame(size_t frame_size) override {
    if (decompressed_.size() + frame_size > uncompressed_size_) {
      frame_size = uncompressed_size_ - decompressed_.size();
    }

    assert(frame_size > 0);

    size_t offset = decompressed_.size();
    decompressed_.resize(offset + frame_size);

    std::copy(data_ + offset, data_ + offset + frame_size,
              decompressed_.data() + offset);

    return decompressed_.size() == uncompressed_size_;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  mutable_byte_buffer decompressed_;
  uint8_t const* const data_;
  size_t const uncompressed_size_;
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
  make_decompressor(std::span<uint8_t const> data,
                    mutable_byte_buffer target) const override {
    return std::make_unique<null_block_decompressor>(data.data(), data.size(),
                                                     target);
  }

 private:
  std::vector<std::string> const options_{};
};

} // namespace

REGISTER_COMPRESSION_FACTORY(null_compression_factory)

} // namespace dwarfs
