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

#include <limits>

#include <fmt/format.h>

#include <dwarfs/error.h>

#include <dwarfs/internal/detail/align_advise_range.h>
#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/internal/memory_mapping_ops.h>

namespace dwarfs::internal {

namespace detail {

static inline size_t align_down(size_t x, size_t g) { return x - (x % g); }

static inline size_t align_up(size_t x, size_t g) {
  size_t const r = x % g;
  return r ? (x + (g - r)) : x;
}

advise_range align_advise_range(advise_range const& input,
                                advise_range_constraints const& constraints,
                                io_advice_range range, std::error_code& ec) {
  ec.clear();

  auto const gran = constraints.granularity;

  // check preconditions
  if (gran == 0 || constraints.page_offset >= gran ||
      input.offset >
          std::numeric_limits<size_t>::max() - constraints.page_offset ||
      constraints.page_offset + input.offset > constraints.mapped_size) {
    ec = make_error_code(std::errc::invalid_argument);
    return {};
  }

  if (input.size == 0) {
    return {};
  }

  size_t start = constraints.page_offset + input.offset;
  size_t end = constraints.mapped_size;

  if (input.size < constraints.mapped_size - start) {
    end = start + input.size;
  }

  if (range == io_advice_range::include_partial) {
    start = align_down(start, gran);
    // end can remain unaligned
  } else {
    start = align_up(start, gran);
    end = align_down(end, gran);
  }

  if (start > constraints.mapped_size) {
    return {};
  }

  size_t const size = end > start ? end - start : 0;

  // check postconditions
  assert(start % gran == 0);
  assert(end <= constraints.mapped_size);
  assert(start <= constraints.mapped_size);
  assert(size <= constraints.mapped_size - start);
  assert(range == io_advice_range::include_partial || size % gran == 0);

  return {start, size};
}

} // namespace detail

namespace {

void handle_error(char const* what, std::error_code* ec,
                  std::error_code const& error) {
  if (error) {
    if (ec) {
      *ec = error;
    } else {
      throw std::system_error{error, what};
    }
  }
}

struct virtual_alloc_tag {};
constexpr virtual_alloc_tag virtual_alloc{};

class memory_mapping_ final : public dwarfs::detail::memory_mapping_impl {
 public:
  memory_mapping_(memory_mapping_ops const& ops, void* addr, size_t page_offset,
                  size_t mapped_size, file_range range, bool readonly,
                  size_t granularity)
      : addr_{addr}
      , page_offset_{page_offset}
      , mapped_size_{mapped_size}
      , range_{range}
      , readonly_{readonly}
      , granularity_{granularity}
      , ops_{ops} {}

  memory_mapping_(virtual_alloc_tag, memory_mapping_ops const& ops, void* addr,
                  size_t page_offset, size_t mapped_size, file_range range,
                  bool readonly, size_t granularity)
      : addr_{addr}
      , page_offset_{page_offset}
      , mapped_size_{mapped_size}
      , range_{range}
      , readonly_{readonly}
      , is_virtual_{true}
      , granularity_{granularity}
      , ops_{ops} {}

  memory_mapping_(memory_mapping_&&) = delete;
  memory_mapping_& operator=(memory_mapping_&&) = delete;
  memory_mapping_(memory_mapping_ const&) = delete;
  memory_mapping_& operator=(memory_mapping_ const&) = delete;

  ~memory_mapping_() override {
    if (addr_ != nullptr) {
      std::error_code ec;
      if (is_virtual_) {
        ops_.virtual_free(addr_, mapped_size_, ec);
      } else {
        ops_.unmap(addr_, mapped_size_, ec);
      }
      DWARFS_CHECK(!ec, fmt::format("{}({}, {}) failed: {}",
                                    is_virtual_ ? "virtual_free" : "unmap",
                                    addr_, mapped_size_, ec.message()));
    }
  }

  file_range range() const override { return range_; }

  std::span<std::byte> mutable_span() override {
    if (readonly_) {
      throw std::runtime_error("memory mapping is read-only");
    }
    return {reinterpret_cast<std::byte*>(addr_) + page_offset_,
            static_cast<size_t>(range_.size())};
  }

  std::span<std::byte const> const_span() const override {
    return {reinterpret_cast<std::byte const*>(addr_) + page_offset_,
            static_cast<size_t>(range_.size())};
  }

  void advise(io_advice advice, size_t offset, size_t size,
              io_advice_range range, std::error_code* ec) const override {
    std::error_code local_ec;

    auto const aligned =
        detail::align_advise_range({.offset = offset, .size = size},
                                   {.page_offset = page_offset_,
                                    .mapped_size = mapped_size_,
                                    .granularity = granularity_},
                                   range, local_ec);

    if (local_ec) {
      handle_error("align_advise_range", ec, local_ec);
      return;
    }

    if (aligned.size == 0) {
      return;
    }

    auto const addr = reinterpret_cast<std::byte*>(addr_) + aligned.offset;

    ops_.advise(addr, aligned.size, advice, local_ec);

    handle_error("advise", ec, local_ec);
  }

  void lock(size_t offset, size_t size, std::error_code* ec) const override {
    offset += page_offset_;

    auto const addr = reinterpret_cast<std::byte*>(addr_) + offset;

    std::error_code local_ec;
    ops_.lock(addr, size, local_ec);

    handle_error("lock", ec, local_ec);
  }

 private:
  void* const addr_{nullptr};
  size_t const page_offset_{0};
  size_t const mapped_size_{0};
  file_range const range_{};
  bool const readonly_{true};
  bool const is_virtual_{false};
  size_t const granularity_{0};
  memory_mapping_ops const& ops_;
};

class mappable_file_ final : public mappable_file::impl {
 public:
  mappable_file_(memory_mapping_ops const& ops, std::any handle,
                 file_size_t size)
      : handle_{std::move(handle)}
      , size_{size}
      , ops_{ops} {}

  mappable_file_(mappable_file_&& other) = delete;
  mappable_file_& operator=(mappable_file_&& other) = delete;
  mappable_file_(mappable_file_ const&) = delete;
  mappable_file_& operator=(mappable_file_ const&) = delete;

  ~mappable_file_() override {
    if (handle_.has_value()) {
      std::error_code ec;
      ops_.close(handle_, ec);
      DWARFS_CHECK(!ec, "close() failed: " + ec.message());
    }
  }

  file_size_t size(std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }
    return size_;
  }

  std::vector<dwarfs::detail::file_extent_info>
  get_extents(std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    std::error_code local_ec;
    auto extents = ops_.get_extents(handle_, local_ec);

    if (local_ec) {
      handle_error("get_file_extents", ec, local_ec);
      return {};
    }

    return extents;
  }

  readonly_memory_mapping map_readonly(std::optional<file_range> range,
                                       std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    file_off_t offset = 0;
    file_size_t size = size_;

    if (range) {
      offset = range->offset();
      size = range->size();
    }

    auto const granularity = ops_.granularity();
    auto const misalign = offset % granularity;
    auto const map_offset = offset - misalign;
    auto const map_size = size + misalign;

    void* addr{nullptr};

    if (size > 0) {
      std::error_code local_ec;
      addr = ops_.map(handle_, map_offset, map_size, local_ec);

      if (local_ec) {
        handle_error("map", ec, local_ec);
        return {};
      }
    }

    return readonly_memory_mapping{std::make_unique<memory_mapping_>(
        ops_, addr, static_cast<size_t>(misalign),
        static_cast<size_t>(map_size), file_range{offset, size}, true,
        granularity)};
  }

  size_t read(std::span<std::byte> buffer, std::optional<file_range> range,
              std::error_code* ec) const override {
    if (ec) {
      ec->clear();
    }

    file_off_t offset = 0;
    file_size_t size = size_;

    if (range) {
      offset = range->offset();
      size = range->size();
    }

    size = std::min(size, static_cast<file_size_t>(buffer.size()));

    std::error_code local_ec;
    auto const rv = ops_.pread(handle_, buffer.data(),
                               static_cast<size_t>(size), offset, local_ec);

    handle_error("pread", ec, local_ec);

    return rv;
  }

 private:
  std::any handle_;
  file_size_t size_{0};
  memory_mapping_ops const& ops_;
};

std::unique_ptr<memory_mapping_>
create_empty_mapping(memory_mapping_ops const& ops, size_t size,
                     memory_access access, std::error_code& ec) {
  ec.clear();

  auto const addr = ops.virtual_alloc(size, access, ec);

  if (ec) {
    return nullptr;
  }

  return std::make_unique<memory_mapping_>(
      virtual_alloc, ops, addr, 0, size, file_range{0, size},
      access == memory_access::readonly, ops.granularity());
}

} // namespace

std::vector<dwarfs::detail::file_extent_info>
mappable_file::get_extents_noexcept() const noexcept {
  std::error_code ec;
  auto extents = impl_->get_extents(&ec);

  if (ec) {
    extents.clear();
    auto const size = impl_->size(&ec);

    if (!ec && size > 0) {
      extents.emplace_back(extent_kind::data, file_range{0, size});
    }
  }

  return extents;
}

readonly_memory_mapping
mappable_file::map_empty_readonly(memory_mapping_ops const& ops, size_t size,
                                  std::error_code& ec) {
  return readonly_memory_mapping{
      create_empty_mapping(ops, size, memory_access::readonly, ec)};
}

readonly_memory_mapping
mappable_file::map_empty_readonly(memory_mapping_ops const& ops, size_t size) {
  std::error_code ec;
  auto mapping = map_empty_readonly(ops, size, ec);
  if (ec) {
    throw std::system_error{ec, "map_empty_readonly"};
  }
  return mapping;
}

memory_mapping mappable_file::map_empty(memory_mapping_ops const& ops,
                                        size_t size, std::error_code& ec) {
  return memory_mapping{
      create_empty_mapping(ops, size, memory_access::readwrite, ec)};
}

memory_mapping
mappable_file::map_empty(memory_mapping_ops const& ops, size_t size) {
  std::error_code ec;
  auto mapping = map_empty(ops, size, ec);
  if (ec) {
    throw std::system_error{ec, "map_empty"};
  }
  return mapping;
}

mappable_file
mappable_file::create(memory_mapping_ops const& ops,
                      std::filesystem::path const& path, std::error_code& ec) {
  ec.clear();

  auto const handle = ops.open(path, ec);

  if (ec) {
    return {};
  }

  auto const size = ops.size(handle, ec);

  if (ec) {
    ops.close(handle, ec);
    return {};
  }

  return mappable_file{std::make_unique<mappable_file_>(ops, handle, size)};
}

mappable_file mappable_file::create(memory_mapping_ops const& ops,
                                    std::filesystem::path const& path) {
  std::error_code ec;
  auto file = create(ops, path, ec);
  if (ec) {
    throw std::system_error{ec, "create"};
  }
  return file;
}

} // namespace dwarfs::internal
