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

#include <atomic>
#include <cstddef>
#include <mutex>

#include <fmt/format.h>

#include <dwarfs/checksum.h>
#include <dwarfs/error.h>
#include <dwarfs/mmif.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/internal/fs_section_checker.h>

namespace dwarfs::internal {

namespace {

template <typename T>
void read_section_header_common(T& header, size_t& start, mmif const& mm,
                                size_t offset) {
  if (offset + sizeof(T) > mm.size()) {
    DWARFS_THROW(runtime_error,
                 fmt::format("truncated section header: {} + {} > {}", offset,
                             sizeof(T), mm.size()));
  }

  ::memcpy(&header, mm.as<void>(offset), sizeof(T));

  offset += sizeof(T);

  auto end = offset + header.length;

  if (end < offset) {
    DWARFS_THROW(runtime_error,
                 fmt::format("offset/length overflow: {} < {}", end, offset));
  }

  if (end > mm.size()) {
    DWARFS_THROW(runtime_error, fmt::format("truncated section data: {} > {}",
                                            end, mm.size()));
  }

  start = offset;
}

template <typename T>
void check_section(T const& sec) {
  if (!is_known_section_type(sec.type())) {
    DWARFS_THROW(runtime_error, fmt::format("unknown section type ({0})",
                                            static_cast<int>(sec.type())));
  }

  if (!is_known_compression_type(sec.compression())) {
    DWARFS_THROW(runtime_error,
                 fmt::format("unknown compression type ({0})",
                             static_cast<int>(sec.compression())));
  }
}

} // namespace

class fs_section_v1 final : public fs_section::impl {
 public:
  fs_section_v1(mmif const& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }

  bool is_known_compression() const override {
    return is_known_compression_type(this->compression());
  }

  bool is_known_type() const override {
    return is_known_section_type(this->type());
  }

  compression_type compression() const override {
    return static_cast<compression_type>(hdr_.compression);
  }
  section_type type() const override { return hdr_.type; }

  std::string name() const override { return get_section_name(hdr_.type); }
  std::string description() const override {
    return fmt::format("{}, offset={}", hdr_.to_string(), start());
  }

  bool check_fast(mmif const&) const override { return true; }

  std::span<uint8_t const> data(mmif const& mm) const override {
    return mm.span(start_, hdr_.length);
  }

  std::optional<std::span<uint8_t const>>
  checksum_span(mmif const&) const override {
    return std::nullopt;
  }

  std::optional<std::span<uint8_t const>>
  integrity_span(mmif const&) const override {
    return std::nullopt;
  }

  std::optional<uint32_t> section_number() const override {
    return std::nullopt;
  }

  std::optional<uint64_t> xxh3_64_value() const override {
    return std::nullopt;
  }

  std::optional<std::span<uint8_t const>> sha2_512_256_value() const override {
    return std::nullopt;
  }

 private:
  size_t start_;
  section_header hdr_;
};

class fs_section_v2 final : public fs_section::impl {
 public:
  fs_section_v2(mmif const& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }

  bool is_known_compression() const override {
    return is_known_compression_type(this->compression());
  }

  bool is_known_type() const override {
    return is_known_section_type(this->type());
  }

  compression_type compression() const override {
    return static_cast<compression_type>(hdr_.compression);
  }

  section_type type() const override {
    return static_cast<section_type>(hdr_.type);
  }

  std::string name() const override {
    return get_section_name(static_cast<section_type>(hdr_.type));
  }

  std::string description() const override {
    std::string_view checksum_status;
    switch (check_state_.load()) {
    case check_state::passed:
      checksum_status = "OK";
      break;
    case check_state::failed:
      checksum_status = "CHECKSUM ERROR";
      break;
    default:
      checksum_status = "unknown";
      break;
    }
    return fmt::format("{} [{}], offset={}", hdr_.to_string(), checksum_status,
                       start());
  }

  bool check_fast(mmif const& mm) const override {
    if (auto state = check_state_.load(); state != check_state::unknown) {
      return state == check_state::passed;
    }

    auto ok = fs_section_checker(mm).check(*this);

    if (auto state = check_state_.load(); state == check_state::failed) {
      ok = false;
    } else {
      auto desired = ok ? check_state::passed : check_state::failed;
      check_state_.compare_exchange_strong(state, desired);
    }

    return ok;
  }

  std::span<uint8_t const> data(mmif const& mm) const override {
    return mm.span(start_, hdr_.length);
  }

  std::optional<std::span<uint8_t const>>
  checksum_span(mmif const& mm) const override {
    static constexpr auto kHdrCsLen =
        sizeof(section_header_v2) - offsetof(section_header_v2, number);

    return mm.span(start_ - kHdrCsLen, hdr_.length + kHdrCsLen);
  }

  std::optional<std::span<uint8_t const>>
  integrity_span(mmif const& mm) const override {
    static constexpr auto kHdrShaLen =
        sizeof(section_header_v2) - offsetof(section_header_v2, xxh3_64);

    return mm.span(start_ - kHdrShaLen, hdr_.length + kHdrShaLen);
  }

  std::optional<uint32_t> section_number() const override {
    return hdr_.number;
  }

  std::optional<uint64_t> xxh3_64_value() const override {
    return hdr_.xxh3_64;
  }

  std::optional<std::span<uint8_t const>> sha2_512_256_value() const override {
    return std::span{hdr_.sha2_512_256.data(), hdr_.sha2_512_256.size()};
  }

 private:
  enum class check_state { unknown, passed, failed };

  size_t start_;
  section_header_v2 hdr_;
  std::atomic<check_state> mutable check_state_{check_state::unknown};
};

class fs_section_v2_lazy final : public fs_section::impl {
 public:
  fs_section_v2_lazy(std::shared_ptr<mmif const> mm, section_type type,
                     size_t offset, size_t size);

  size_t start() const override { return offset_ + sizeof(section_header_v2); }
  size_t length() const override { return size_ - sizeof(section_header_v2); }

  bool is_known_compression() const override {
    return is_known_compression_type(this->compression());
  }

  bool is_known_type() const override {
    return is_known_section_type(this->type());
  }

  compression_type compression() const override {
    return section().compression();
  }

  section_type type() const override { return type_; }

  std::string name() const override { return get_section_name(type_); }

  std::string description() const override { return section().description(); }

  bool check_fast(mmif const& mm) const override {
    return section().check_fast(mm);
  }

  std::span<uint8_t const> data(mmif const& mm) const override {
    return section().data(mm);
  }

  std::optional<std::span<uint8_t const>>
  checksum_span(mmif const& mm) const override {
    return section().checksum_span(mm);
  }

  std::optional<std::span<uint8_t const>>
  integrity_span(mmif const& mm) const override {
    return section().integrity_span(mm);
  }

  std::optional<uint32_t> section_number() const override {
    return section().section_number();
  }

  std::optional<uint64_t> xxh3_64_value() const override {
    return section().xxh3_64_value();
  }

  std::optional<std::span<uint8_t const>> sha2_512_256_value() const override {
    return section().sha2_512_256_value();
  }

 private:
  fs_section::impl const& section() const;

  std::mutex mutable mx_;
  std::unique_ptr<fs_section::impl const> mutable sec_;
  std::shared_ptr<mmif const> mutable mm_;
  section_type const type_;
  size_t const offset_;
  size_t const size_;
};

fs_section::fs_section(mmif const& mm, size_t offset, int version) {
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

fs_section::fs_section(std::shared_ptr<mmif const> mm, section_type type,
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

fs_section_v1::fs_section_v1(mmif const& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
  check_section(*this);
}

fs_section_v2::fs_section_v2(mmif const& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
  // TODO: Don't enforce these checks as we might want to add section types
  //       and compression types in the future without necessarily incrementing
  //       the file system version.
  //       Only enforce them for v1 above, which doesn't have checksums and
  //       where we know the exact set of section and compression types.
  // check_section(*this);
}

fs_section_v2_lazy::fs_section_v2_lazy(std::shared_ptr<mmif const> mm,
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

} // namespace dwarfs::internal
