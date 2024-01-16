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

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"

namespace dwarfs {

namespace {

class null_block_compressor final : public block_compressor::impl {
 public:
  null_block_compressor() = default;
  null_block_compressor(const null_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<null_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data,
           std::string const* /*metadata*/) const override {
    return data;
  }

  std::vector<uint8_t>
  compress(std::vector<uint8_t>&& data,
           std::string const* /*metadata*/) const override {
    return std::move(data);
  }

  compression_type type() const override { return compression_type::NONE; }

  std::string describe() const override { return "null"; }

  std::string metadata_requirements() const override { return std::string(); }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return compression_constraints();
  }
};

class null_compression_factory : public compression_factory {
 public:
  std::string_view name() const override { return "null"; }

  std::string_view description() const override {
    return "no compression at all";
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map&) const override {
    return std::make_unique<null_block_compressor>();
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const>,
                    std::vector<uint8_t>&) const override {
    DWARFS_THROW(runtime_error, "null_decompressor not implemented");
  }

 private:
  std::vector<std::string> const options_{};
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::NONE, null_compression_factory)

} // namespace dwarfs
