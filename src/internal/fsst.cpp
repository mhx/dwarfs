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

#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

#include <dwarfs/container/packed_int_vector.h>
#include <dwarfs/error.h>
#include <dwarfs/internal/fsst.h>

#include <fmt/format.h>

#include <fsst.h>

namespace dwarfs::internal {

namespace {

using encoder_handle =
    std::unique_ptr<::fsst_encoder_t, decltype(&::fsst_destroy)>;

/**
 * The number of pieces `fsst_compress()` will split a batch into, and their
 * total byte length.
 *
 * `compressBulk()` processes each string in pieces of at most 511 bytes and
 * needs `2 * piece + 7` bytes of output space available for each one. Every
 * string counts as at least one piece, empty strings included.
 */
struct batch_extent {
  std::size_t total_length{0};
  std::size_t pieces{0};

  [[nodiscard]] std::size_t output_space_needed() const {
    return 2 * total_length + 7 * pieces;
  }
};

/**
 * Adapter presenting contiguous pointer and length arrays as a string source.
 */
class array_string_source {
 public:
  using value_type = std::string_view;

  array_string_source(std::span<unsigned char const*> ptr,
                      std::span<std::size_t const> len)
      : ptr_{ptr}
      , len_{len} {
    assert(ptr_.size() == len_.size());
  }

  [[nodiscard]] std::size_t size() const { return ptr_.size(); }

  [[nodiscard]] std::string_view operator[](std::size_t i) const {
    return {reinterpret_cast<char const*>(ptr_[i]), len_[i]};
  }

 private:
  std::span<unsigned char const*> ptr_;
  std::span<std::size_t const> len_;
};

encoder_handle create_encoder(fsst_string_source const& source) {
  ::fsst_input_t input;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  input.context = const_cast<fsst_string_source*>(&source);
  input.length = [](void* ctx, std::size_t i) -> std::size_t {
    return static_cast<fsst_string_source const*>(ctx)->length(i);
  };
  input.data = [](void* ctx, std::size_t i) -> unsigned char const* {
    return static_cast<fsst_string_source const*>(ctx)->data(i);
  };
  input.lengths = nullptr;

  encoder_handle enc{::fsst_create_indexed(source.size(), &input, 0),
                     &::fsst_destroy};

  DWARFS_CHECK(enc != nullptr, "failed to create FSST encoder");

  return enc;
}

std::string export_dictionary(::fsst_encoder_t* enc) {
  std::string dictionary;
  dictionary.resize(sizeof(::fsst_decoder_t));
  auto const size =
      ::fsst_export(enc, reinterpret_cast<unsigned char*>(dictionary.data()));
  dictionary.resize(size);
  return dictionary;
}

class fsst_decoder_ : public fsst_decoder::impl {
 public:
  explicit fsst_decoder_(std::string_view dictionary) {
    assert(!dictionary.empty());
    auto const* header =
        reinterpret_cast<unsigned char const*>(dictionary.data());
    auto const read = ::fsst_validate_header(header, dictionary.size());
    if (read == 0) {
      throw std::runtime_error("invalid FSST dictionary");
    }
    if (read != dictionary.size()) {
      throw std::runtime_error(fmt::format(
          "read {0} symtab bytes, expected {1}", read, dictionary.size()));
    }
    auto const read2 = ::fsst_import(&decoder_, header);
    DWARFS_CHECK(
        read2 == read,
        fmt::format("imported {0} symtab bytes, expected {1}", read2, read));
  }

  void
  decompress_append_to(std::string& out, std::string_view data) const override {
    decompress_append_to_impl(out, data);
  }

  void decompress_append_to_u8(std::u8string& out,
                               std::u8string_view data) const override {
    decompress_append_to_impl(out, data);
  }

 private:
  template <typename StringType, typename StringViewType>
    requires std::same_as<typename StringType::value_type,
                          typename StringViewType::value_type>
  void decompress_append_to_impl(StringType& out, StringViewType data) const {
    assert(fsst_decoder::is_valid_compressed_string(std::string_view{
        reinterpret_cast<char const*>(data.data()), data.size()}));
    thread_local StringType buf;
    auto const size = data.size();
    buf.resize(std::max<size_t>(1, 8 * size));
    auto outlen = ::fsst_decompress(
        &decoder_, size, reinterpret_cast<unsigned char const*>(data.data()),
        buf.size(), reinterpret_cast<unsigned char*>(buf.data()));
    buf.resize(outlen);
    out.append(buf);
  }

  ::fsst_decoder_t decoder_;
};

} // namespace

namespace detail {

/**
 * Shared implementation behind fsst_incremental_compressor.
 *
 * Deliberately not templated: every string type the public interface supports
 * has single-byte characters, so one implementation covers all of them.
 */
class fsst_incremental_compressor_impl {
 public:
  using index_type = fsst_encoder::index_type;
  using position_type = index_type::value_type;

  // Strings per call to fsst_compress(). Bounds the pointer and length arrays
  // at a few tens of kilobytes regardless of how much is being compressed.
  static constexpr std::size_t max_batch_size{4096};

  static constexpr std::size_t max_position{
      std::numeric_limits<position_type>::max()};

  explicit fsst_incremental_compressor_impl(fsst_string_source const& source)
      : enc_{create_encoder(source)}
      , dictionary_{export_dictionary(enc_.get())}
      , total_input_size_{source.total_length()} {
    // Positions are offsets into the compressed buffer, which is normally
    // smaller than the input, so sizing elements for the uncompressed length
    // means the vector should not have to be repacked while being filled.
    positions_.reset(index_type::required_bits(
        static_cast<position_type>(std::min(total_input_size_, max_position))));
    positions_.reserve(source.size() + 1);
    positions_.push_back(0);
  }

  [[nodiscard]] std::size_t dictionary_size() const {
    return dictionary_.size();
  }
  [[nodiscard]] std::size_t total_input_size() const {
    return total_input_size_;
  }
  [[nodiscard]] std::size_t count() const { return count_; }
  [[nodiscard]] std::size_t input_size() const { return input_size_; }
  [[nodiscard]] std::size_t compressed_size() const { return compressed_size_; }

  [[nodiscard]] std::size_t
  estimated_compressed_size(fsst_string_source const& sample) const {
    std::vector<unsigned char const*> ptr;
    std::vector<std::size_t> len;
    std::vector<std::size_t> out_len;
    std::string scratch;
    std::size_t total = 0;

    for_each_batch(sample.size(), [&](std::size_t offset, std::size_t count) {
      auto const extent = gather(sample, offset, count, ptr, len);

      if (scratch.size() < extent.output_space_needed()) {
        scratch.resize(extent.output_space_needed());
      }
      out_len.resize(count);

      auto const num = ::fsst_compress(
          enc_.get(), count, len.data(), ptr.data(), scratch.size(),
          reinterpret_cast<unsigned char*>(scratch.data()), out_len.data(),
          nullptr);

      DWARFS_CHECK(num == count,
                   "FSST scratch buffer too small while estimating");

      total += std::accumulate(out_len.begin(), out_len.end(), std::size_t{0});
    });

    return total;
  }

  void add(fsst_string_source const& strings) {
    for_each_batch(strings.size(), [&](std::size_t offset, std::size_t count) {
      add_batch(strings, offset, count);
    });

    count_ += strings.size();
  }

  [[nodiscard]] fsst_encoder::bulk_compression_result finish() {
    fsst_encoder::bulk_compression_result result;

    buffer_.resize(compressed_size_);

    result.dictionary = std::move(dictionary_);
    result.buffer = std::move(buffer_);

    // Narrow the elements to what the largest actual position needs, then move
    // into the fixed-width vector. Both use the same storage layout, so the
    // move itself does not copy.
    positions_.optimize_storage();
    result.positions = std::move(positions_);

    return result;
  }

 private:
  /**
   * Split `size` elements into batches and invoke `fn(offset, count)` on each.
   *
   * Compressing in batches keeps the pointer and length arrays that
   * `fsst_compress()` requires bounded. The batch size is chosen to stay
   * comfortably above the thresholds `fsst_compress()` uses to select its
   * SIMD path, so batching costs nothing in throughput.
   */
  template <typename Fn>
  static void for_each_batch(std::size_t size, Fn&& fn) {
    auto&& func = std::forward<Fn>(fn);
    for (std::size_t offset = 0; offset < size; offset += max_batch_size) {
      func(offset, std::min(max_batch_size, size - offset));
    }
  }

  static batch_extent
  gather(fsst_string_source const& source, std::size_t offset,
         std::size_t count, std::vector<unsigned char const*>& ptr,
         std::vector<std::size_t>& len) {
    ptr.clear();
    len.clear();
    ptr.reserve(count);
    len.reserve(count);

    batch_extent extent;

    for (std::size_t i = offset; i < offset + count; ++i) {
      auto const length = source.length(i);
      ptr.emplace_back(source.data(i));
      len.emplace_back(length);
      extent.total_length += length;
      extent.pieces += 1 + length / 511;
    }

    return extent;
  }

  void add_batch(fsst_string_source const& strings, std::size_t offset,
                 std::size_t count) {
    auto const extent = gather(strings, offset, count, ptr_, len_);

    grow_buffer_to(compressed_size_ + extent.output_space_needed());

    out_len_.resize(count);

    for (;;) {
      auto const num = ::fsst_compress(
          enc_.get(), count, len_.data(), ptr_.data(),
          buffer_.size() - compressed_size_,
          reinterpret_cast<unsigned char*>(buffer_.data()) + compressed_size_,
          out_len_.data(), nullptr);

      if (num == count) {
        break;
      }

      // Unreachable given the bound above, but growing and retrying is much
      // better than silently truncating should that bound ever be wrong.
      grow_buffer_to(2 * buffer_.size());
    }

    auto const batch_output_size =
        std::accumulate(out_len_.begin(), out_len_.end(), std::size_t{0});

    DWARFS_CHECK(batch_output_size <= max_position - compressed_size_,
                 "FSST compressed data too large for 32-bit positions");

    for (auto const len : out_len_) {
      compressed_size_ += len;
      positions_.push_back(static_cast<position_type>(compressed_size_));
    }

    input_size_ += extent.total_length;
  }

  /**
   * Projected size of the whole compressed result, from what has been
   * compressed so far, with some headroom.
   */
  [[nodiscard]] std::size_t projected_output_size() const {
    if (input_size_ == 0 || compressed_size_ == 0) {
      return 0;
    }

    auto const ratio = static_cast<double>(compressed_size_) /
                       static_cast<double>(input_size_);

    return static_cast<std::size_t>(1.05 * ratio *
                                    static_cast<double>(total_input_size_));
  }

  /**
   * Make at least `size` bytes of buffer available.
   *
   * The capacity is reserved explicitly rather than left to the string's own
   * geometric growth, which would otherwise end up holding close to twice the
   * result. Once one batch has been compressed there is a ratio to extrapolate
   * from, so in practice this reserves the whole result in a single step.
   */
  void grow_buffer_to(std::size_t size) {
    if (buffer_.size() >= size) {
      return;
    }

    if (buffer_.capacity() < size) {
      buffer_.reserve(std::max(size, projected_output_size()));
    }

    buffer_.resize(size);
  }

  encoder_handle enc_;
  std::string dictionary_;
  std::string buffer_;
  dwarfs::container::auto_packed_int_vector<position_type> positions_;
  std::vector<unsigned char const*> ptr_;
  std::vector<std::size_t> len_;
  std::vector<std::size_t> out_len_;
  std::size_t total_input_size_;
  std::size_t count_{0};
  std::size_t input_size_{0};
  std::size_t compressed_size_{0};
};

} // namespace detail

std::size_t fsst_string_source::total_length() const {
  std::size_t total = 0;

  for (std::size_t i = 0; i < size_; ++i) {
    total += length_(context_, i);
  }

  return total;
}

// ---------------------------------------------------------------------------
// fsst_incremental_compressor
// ---------------------------------------------------------------------------

template <detail::fsst_string_type StringType>
fsst_incremental_compressor<StringType>::fsst_incremental_compressor(
    std::unique_ptr<detail::fsst_incremental_compressor_impl> impl)
    : impl_{std::move(impl)} {}

template <detail::fsst_string_type StringType>
fsst_incremental_compressor<StringType>::fsst_incremental_compressor(
    fsst_incremental_compressor&&) noexcept = default;

template <detail::fsst_string_type StringType>
auto fsst_incremental_compressor<StringType>::operator=(
    fsst_incremental_compressor&&) noexcept
    -> fsst_incremental_compressor& = default;

template <detail::fsst_string_type StringType>
fsst_incremental_compressor<StringType>::~fsst_incremental_compressor() =
    default;

template <detail::fsst_string_type StringType>
auto fsst_incremental_compressor<StringType>::create(
    fsst_string_source const& source) -> fsst_incremental_compressor {
  return fsst_incremental_compressor{
      std::make_unique<detail::fsst_incremental_compressor_impl>(source)};
}

template <detail::fsst_string_type StringType>
std::size_t fsst_incremental_compressor<StringType>::estimated_compressed_size(
    fsst_string_source const& sample) const {
  return impl_->estimated_compressed_size(sample);
}

template <detail::fsst_string_type StringType>
void fsst_incremental_compressor<StringType>::add(
    fsst_string_source const& strings) {
  impl_->add(strings);
}

template <detail::fsst_string_type StringType>
std::size_t fsst_incremental_compressor<StringType>::dictionary_size() const {
  return impl_->dictionary_size();
}

template <detail::fsst_string_type StringType>
std::size_t fsst_incremental_compressor<StringType>::count() const {
  return impl_->count();
}

template <detail::fsst_string_type StringType>
std::size_t fsst_incremental_compressor<StringType>::input_size() const {
  return impl_->input_size();
}

template <detail::fsst_string_type StringType>
std::size_t fsst_incremental_compressor<StringType>::compressed_size() const {
  return impl_->compressed_size();
}

template <detail::fsst_string_type StringType>
auto fsst_incremental_compressor<StringType>::finish() && -> result_type {
  return impl_->finish();
}

template class fsst_incremental_compressor<std::string>;
template class fsst_incremental_compressor<std::string_view>;
template class fsst_incremental_compressor<std::u8string>;
template class fsst_incremental_compressor<std::u8string_view>;

// ---------------------------------------------------------------------------
// fsst_encoder
// ---------------------------------------------------------------------------

namespace {

/**
 * One-shot compression on top of the incremental compressor.
 *
 * Unlike the incremental interface, this can decide after the fact whether
 * compressing was worthwhile, because the input is still around.
 */
std::optional<fsst_encoder::bulk_compression_result>
compress_one_shot(fsst_string_source const& source, bool force) {
  if (source.empty()) {
    return std::nullopt;
  }

  detail::fsst_incremental_compressor_impl compressor{source};

  auto const total_input_size = compressor.total_input_size();

  if (compressor.dictionary_size() >= total_input_size && !force) {
    return std::nullopt;
  }

  compressor.add(source);

  if (compressor.dictionary_size() + compressor.compressed_size() >=
          total_input_size &&
      !force) {
    return std::nullopt;
  }

  return compressor.finish();
}

} // namespace

auto fsst_encoder::compress(fsst_string_source const& source, bool force)
    -> std::optional<bulk_compression_result> {
  return compress_one_shot(source, force);
}

auto fsst_encoder::compress(std::span<std::string_view const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress_one_shot(fsst_string_source{data}, force);
}

auto fsst_encoder::compress(std::span<std::string const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress_one_shot(fsst_string_source{data}, force);
}

auto fsst_encoder::compress(std::span<std::u8string_view const> data,
                            bool force)
    -> std::optional<bulk_compression_result> {
  return compress_one_shot(fsst_string_source{data}, force);
}

auto fsst_encoder::compress(std::span<std::u8string const> data, bool force)
    -> std::optional<bulk_compression_result> {
  return compress_one_shot(fsst_string_source{data}, force);
}

auto fsst_encoder::compress(std::span<unsigned char const*> ptr_span,
                            std::span<size_t const> len_span,
                            std::size_t const /*total_input_size*/, bool force)
    -> std::optional<bulk_compression_result> {
  array_string_source const arrays{ptr_span, len_span};
  return compress_one_shot(fsst_string_source{arrays}, force);
}

// ---------------------------------------------------------------------------
// fsst_decoder
// ---------------------------------------------------------------------------

fsst_decoder::fsst_decoder(std::string_view dictionary)
    : impl_{std::make_unique<fsst_decoder_>(dictionary)} {}

bool fsst_decoder::is_valid_dictionary(std::string_view dictionary) {
  if (dictionary.empty()) {
    return false;
  }

  auto const* const header =
      reinterpret_cast<unsigned char const*>(dictionary.data());
  auto const read = ::fsst_validate_header(header, dictionary.size());

  return read == dictionary.size();
}

bool fsst_decoder::is_valid_compressed_string(std::string_view str) {
  auto const* const buf = reinterpret_cast<unsigned char const*>(str.data());
  return ::fsst_validate_compressed(buf, str.size()) != 0;
}

} // namespace dwarfs::internal
