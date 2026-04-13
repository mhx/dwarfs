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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <dwarfs/internal/associative_vector_types.h>
#include <dwarfs/writer/internal/inode.h>

namespace dwarfs::writer {

struct inode_options;

namespace internal {

class scanner_progress;

namespace detail {

enum class scan_mode {
  skip_holes,
  include_holes,
};

class inode_impl final : public inode {
 public:
  using chunk_type = thrift::metadata::chunk;

  inode_impl();
  ~inode_impl() override;

  void set_num(uint32_t num) override;
  uint32_t num() const override;

  bool has_category(fragment_category cat) const override;

  std::optional<uint32_t> similarity_hash(fragment_category cat) const override;

  nilsimsa::hash_type const*
  nilsimsa_similarity_hash(fragment_category cat) const override;

  void set_files(file_handle_vector const& fv) override;

  void populate(file_size_t size) override;

  void
  scan(file_view const& mm, inode_options const& opts, progress& prog) override;

  file_size_t size() const override;

  const_file_handle any() const override;

  file_handle_vector all() const override;

  bool append_chunks_to(
      std::vector<chunk_type>& vec,
      std::optional<inode_hole_mapper>& hole_mapper) const override;

  inode_fragments& fragments() override;
  inode_fragments const& fragments() const override;

  void dump(std::ostream& os, inode_options const& options) const override;

  void set_scan_error(const_file_handle fp, std::exception_ptr ep) override;

  std::optional<std::pair<const_file_handle, std::exception_ptr>>
  get_scan_error() const override;

  inode_mmap_any_result
  mmap_any(os_access const& os,
           open_file_options const& of_opts) const override;

 private:
  std::shared_ptr<scanner_progress>
  make_progress_context(std::string_view context, file_view const& mm,
                        progress& prog, size_t min_size) const;

  template <typename T>
  T const* find_similarity(fragment_category cat) const;

  void scan_range(file_view const& mm, scanner_progress* sprog,
                  file_off_t offset, file_size_t size, size_t chunk_size,
                  std::invocable<std::span<uint8_t const>> auto&& scanner,
                  scan_mode mode = scan_mode::skip_holes);

  void
  scan_range(file_view const& mm, scanner_progress* sprog, size_t chunk_size,
             std::invocable<std::span<uint8_t const>> auto&& scanner,
             scan_mode mode = scan_mode::skip_holes);

  void scan_fragments(file_view const& mm, scanner_progress* sprog,
                      inode_options const& opts, size_t chunk_size);

  void scan_full(file_view const& mm, scanner_progress* sprog,
                 inode_options const& opts, size_t chunk_size);

  using similarity_map_type = dwarfs::internal::small_vector_map<
      fragment_category, std::variant<nilsimsa::hash_type, uint32_t>>;

  static constexpr uint32_t const kNumIsValid{UINT32_C(1) << 0};

  uint32_t flags_{0};
  uint32_t num_{0};
  inode_fragments fragments_;
  file_handle_vector files_;
  std::unique_ptr<std::pair<const_file_handle, std::exception_ptr>> scan_error_;

  std::variant<
      // in case of no hashes at all
      std::monostate,

      // in case of only a single fragment
      nilsimsa::hash_type, // 32 bytes
      uint32_t,            //  4 bytes

      // in case of multiple fragments
      similarity_map_type // 24 bytes
      >
      similarity_;

  static_assert(sizeof(similarity_map_type) <= sizeof(nilsimsa::hash_type));
};

} // namespace detail

} // namespace internal

} // namespace dwarfs::writer
