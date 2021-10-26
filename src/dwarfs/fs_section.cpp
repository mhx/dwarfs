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

#include <cstddef>
#include <mutex>

#include <fmt/format.h>

#include "dwarfs/checksum.h"
#include "dwarfs/error.h"
#include "dwarfs/fs_section.h"
#include "dwarfs/mmif.h"

namespace dwarfs {

namespace {

template <typename T>
void read_section_header_common(T& header, size_t& start, mmif& mm,
                                size_t offset) {
  if (offset + sizeof(T) > mm.size()) {
    DWARFS_THROW(runtime_error, "truncated section header");
  }

  ::memcpy(&header, mm.as<void>(offset), sizeof(T));

  offset += sizeof(T);

  auto end = offset + header.length;

  if (end < offset) {
    DWARFS_THROW(runtime_error, "offset/length overflow");
  }

  if (end > mm.size()) {
    DWARFS_THROW(runtime_error, "truncated section data");
  }

  start = offset;
}

template <typename T>
void check_section(T const& sec) {
  if (!is_valid_section_type(sec.type())) {
    DWARFS_THROW(runtime_error, fmt::format("invalid section type ({0})",
                                            static_cast<int>(sec.type())));
  }

  if (!is_valid_compression_type(sec.compression())) {
    DWARFS_THROW(runtime_error,
                 fmt::format("invalid compression type ({0})",
                             static_cast<int>(sec.compression())));
  }
}

} // namespace

class fs_section_v1 : public fs_section::impl {
 public:
  fs_section_v1(mmif& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }

  compression_type compression() const override { return hdr_.compression; }
  section_type type() const override { return hdr_.type; }

  std::string name() const override { return get_section_name(hdr_.type); }
  std::string description() const override { return hdr_.to_string(); }

  bool check_fast(mmif&) const override { return true; }
  bool verify(mmif&) const override { return true; }

  folly::ByteRange data(mmif& mm) const override {
    return folly::ByteRange(mm.as<uint8_t>(start_), hdr_.length);
  }

 private:
  size_t start_;
  section_header hdr_;
};

class fs_section_v2 : public fs_section::impl {
 public:
  fs_section_v2(mmif& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }

  compression_type compression() const override {
    return static_cast<compression_type>(hdr_.compression);
  }

  section_type type() const override {
    return static_cast<section_type>(hdr_.type);
  }

  std::string name() const override {
    return get_section_name(static_cast<section_type>(hdr_.type));
  }

  std::string description() const override { return hdr_.to_string(); }

  bool check_fast(mmif& mm) const override {
    auto hdr_cs_len =
        sizeof(section_header_v2) - offsetof(section_header_v2, number);
    return checksum::verify(checksum::algorithm::XXH3_64,
                            mm.as<void>(start_ - hdr_cs_len),
                            hdr_.length + hdr_cs_len, &hdr_.xxh3_64);
  }

  bool verify(mmif& mm) const override {
    auto hdr_sha_len =
        sizeof(section_header_v2) - offsetof(section_header_v2, xxh3_64);
    return checksum::verify(checksum::algorithm::SHA2_512_256,
                            mm.as<void>(start_ - hdr_sha_len),
                            hdr_.length + hdr_sha_len, &hdr_.sha2_512_256);
  }

  folly::ByteRange data(mmif& mm) const override {
    return folly::ByteRange(mm.as<uint8_t>(start_), hdr_.length);
  }

 private:
  size_t start_;
  section_header_v2 hdr_;
};

class fs_section_v2_lazy : public fs_section::impl {
 public:
  fs_section_v2_lazy(std::shared_ptr<mmif> mm, section_type type, size_t offset,
                     size_t size);

  size_t start() const override { return offset_ + sizeof(section_header_v2); }
  size_t length() const override { return size_ - sizeof(section_header_v2); }

  compression_type compression() const override {
    return section().compression();
  }

  section_type type() const override { return type_; }

  std::string name() const override { return get_section_name(type_); }

  std::string description() const override { return section().description(); }

  bool check_fast(mmif& mm) const override { return section().check_fast(mm); }

  bool verify(mmif& mm) const override { return section().verify(mm); }

  folly::ByteRange data(mmif& mm) const override { return section().data(mm); }

 private:
  fs_section::impl const& section() const;

  std::mutex mutable mx_;
  std::unique_ptr<fs_section::impl const> mutable sec_;
  std::shared_ptr<mmif> mutable mm_;
  section_type type_;
  size_t offset_;
  size_t size_;
};

fs_section::fs_section(mmif& mm, size_t offset, int version) {
  switch (version) {
  case 1:
    impl_ = std::make_shared<fs_section_v1>(mm, offset);
    break;

  case 2:
    impl_ = std::make_shared<fs_section_v2>(mm, offset);
    break;

  default:
    DWARFS_THROW(runtime_error,
                 fmt::format("unsupported section version {}", version));
    break;
  }
}

fs_section::fs_section(std::shared_ptr<mmif> mm, section_type type,
                       size_t offset, size_t size, int version) {
  switch (version) {
  case 2:
    impl_ =
        std::make_shared<fs_section_v2_lazy>(std::move(mm), type, offset, size);
    break;

  default:
    DWARFS_THROW(runtime_error,
                 fmt::format("unsupported section version {} [lazy]", version));
    break;
  }
}

fs_section_v1::fs_section_v1(mmif& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
  check_section(*this);
}

fs_section_v2::fs_section_v2(mmif& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
  check_section(*this);
}

fs_section_v2_lazy::fs_section_v2_lazy(std::shared_ptr<mmif> mm,
                                       section_type type, size_t offset,
                                       size_t size)
    : mm_{std::move(mm)}
    , type_{type}
    , offset_{offset}
    , size_{size} {}

fs_section::impl const& fs_section_v2_lazy::section() const {
  std::lock_guard lock(mx_);

  if (!sec_) {
    sec_ = std::make_unique<fs_section_v2>(*mm_, offset_);
    mm_.reset();
  }

  return *sec_;
}

} // namespace dwarfs
