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

#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include <folly/system/ThreadName.h>

#include <dwarfs/block_decompressor.h>
#include <dwarfs/checksum.h>
#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/compression_metadata_requirements.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/filesystem_writer_options.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/filesystem_writer_detail.h>
#include <dwarfs/writer/internal/multi_queue_block_merger.h>
#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer {

namespace internal {

using namespace dwarfs::internal;

namespace {

size_t copy_stream(std::istream& is, std::ostream& os) {
  std::streambuf* rdbuf = is.rdbuf();
  std::streamsize count{0};
  std::streamsize transferred;
  std::array<char, 1024> buffer;

  while ((transferred = rdbuf->sgetn(buffer.data(), buffer.size())) > 0) {
    os.write(buffer.data(), transferred);
    count += transferred;
  }

  return count;
}

std::string get_friendly_section_name(section_type type) {
  switch (type) {
  case section_type::METADATA_V2_SCHEMA:
    return "schema";
  case section_type::METADATA_V2:
    return "metadata";
  case section_type::HISTORY:
    return "history";
  case section_type::BLOCK:
    return "block";
  case section_type::SECTION_INDEX:
    return "index";
  }

  return get_section_name(type);
}

class compression_progress : public progress::context {
 public:
  using status = progress::context::status;

  compression_progress() = default;

  status get_status() const override {
    status st;
    st.color = termcolor::RED;
    st.context = "[compressing] ";
    auto bin = bytes_in.load();
    auto bout = bytes_out.load();
    if (bin > 0 && bout > 0) {
      st.status_string = fmt::format("compressed {} to {} (ratio {:.2f}%)",
                                     size_with_unit(bin), size_with_unit(bout),
                                     100.0 * bout / bin);
    }
    st.bytes_processed.emplace(bytes_in.load());
    return st;
  }

  int get_priority() const override { return -1000; }

  std::atomic<size_t> bytes_in{0};
  std::atomic<size_t> bytes_out{0};
};

class fsblock {
 public:
  fsblock(section_type type, block_compressor const& bc,
          shared_byte_buffer data, std::shared_ptr<compression_progress> pctx,
          folly::Function<void(size_t)> set_block_cb = nullptr);

  fsblock(section_type type, compression_type compression,
          std::span<uint8_t const> data);

  fsblock(fs_section sec, std::span<uint8_t const> data,
          std::shared_ptr<compression_progress> pctx);

  fsblock(section_type type, block_compressor const& bc,
          delayed_data_fn_type data, size_t uncompressed_size,
          std::shared_ptr<compression_progress> pctx);

  void
  compress(worker_group& wg, std::optional<std::string> meta = std::nullopt) {
    impl_->compress(wg, std::move(meta));
  }
  void wait_until_compressed() { impl_->wait_until_compressed(); }
  section_type type() const { return impl_->type(); }
  compression_type compression() const { return impl_->compression(); }
  std::string description() const { return impl_->description(); }
  std::span<uint8_t const> data() const { return impl_->data(); }
  size_t uncompressed_size() const { return impl_->uncompressed_size(); }
  size_t size() const { return impl_->size(); }
  size_t estimated_mem_usage() const { return impl_->estimated_mem_usage(); }
  void set_block_no(uint32_t number) { impl_->set_block_no(number); }
  uint32_t block_no() const { return impl_->block_no(); }
  section_header_v2 const& header() const { return impl_->header(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    compress(worker_group& wg, std::optional<std::string> meta) = 0;
    virtual void wait_until_compressed() = 0;
    virtual section_type type() const = 0;
    virtual compression_type compression() const = 0;
    virtual std::string description() const = 0;
    virtual std::span<uint8_t const> data() const = 0;
    virtual size_t uncompressed_size() const = 0;
    virtual size_t size() const = 0;
    virtual size_t estimated_mem_usage() const = 0;
    virtual void set_block_no(uint32_t number) = 0;
    virtual uint32_t block_no() const = 0;
    virtual section_header_v2 const& header() const = 0;
  };

  static void
  build_section_header(section_header_v2& sh, fsblock::impl const& fsb,
                       std::optional<fs_section> const& sec = std::nullopt);

 private:
  std::unique_ptr<impl> impl_;
};

class fsblock_merger_policy {
 public:
  explicit fsblock_merger_policy(size_t worst_case_block_size)
      : worst_case_block_size_{worst_case_block_size} {}

  size_t block_size(std::unique_ptr<fsblock> const& fsb) const {
    assert(fsb->size() <= worst_case_block_size_);
    return fsb->size();
  }

  size_t worst_case_source_block_size(fragment_category /*source_id*/) const {
    return worst_case_block_size_;
  }

 private:
  size_t worst_case_block_size_;
};

class raw_fsblock : public fsblock::impl {
 public:
  raw_fsblock(section_type type, block_compressor const& bc,
              shared_byte_buffer data,
              std::shared_ptr<compression_progress> pctx,
              folly::Function<void(size_t)> set_block_cb)
      : type_{type}
      , bc_{bc}
      , uncompressed_size_{data.size()}
      , data_{std::move(data)}
      , comp_type_{bc_.type()}
      , pctx_{std::move(pctx)}
      , set_block_cb_{std::move(set_block_cb)} {
    DWARFS_CHECK(bc_, "block_compressor must not be null");
  }

  void compress(worker_group& wg, std::optional<std::string> meta) override {
    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job(
        [this, prom = std::move(prom), meta = std::move(meta)]() mutable {
          try {
            shared_byte_buffer tmp;

            if (meta) {
              tmp = bc_.compress(data_, *meta);
            } else {
              tmp = bc_.compress(data_);
            }

            pctx_->bytes_in += data_.size();
            pctx_->bytes_out += tmp.size();

            {
              std::lock_guard lock(mx_);
              data_.swap(tmp);
            }
          } catch (bad_compression_ratio_error const&) {
            comp_type_ = compression_type::NONE;
          }

          prom.set_value();
        });
  }

  void wait_until_compressed() override { future_.wait(); }

  section_type type() const override { return type_; }

  compression_type compression() const override { return comp_type_; }

  std::string description() const override { return bc_.describe(); }

  std::span<uint8_t const> data() const override { return data_.span(); }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  size_t size() const override {
    std::lock_guard lock(mx_);
    return data_.size();
  }

  size_t estimated_mem_usage() const override {
    std::lock_guard lock(mx_);
    return data_.capacity();
  }

  void set_block_no(uint32_t number) override {
    {
      std::lock_guard lock(mx_);
      DWARFS_CHECK(!number_.has_value(), "block number already set");
      number_ = number;
    }

    if (set_block_cb_) {
      set_block_cb_(number);
    }
  }

  uint32_t block_no() const override {
    std::lock_guard lock(mx_);
    return number_.value();
  }

  section_header_v2 const& header() const override {
    std::lock_guard lock(mx_);
    if (!header_) {
      header_ = section_header_v2{};
      fsblock::build_section_header(*header_, *this);
    }
    return header_.value();
  }

 private:
  section_type const type_;
  block_compressor const& bc_;
  size_t const uncompressed_size_;
  mutable std::recursive_mutex mx_;
  shared_byte_buffer data_;
  std::future<void> future_;
  std::optional<uint32_t> number_;
  std::optional<section_header_v2> mutable header_;
  compression_type comp_type_;
  std::shared_ptr<compression_progress> pctx_;
  folly::Function<void(size_t)> set_block_cb_;
};

class compressed_fsblock : public fsblock::impl {
 public:
  compressed_fsblock(section_type type, compression_type compression,
                     std::span<uint8_t const> range)
      : type_{type}
      , compression_{compression}
      , range_{range} {}

  compressed_fsblock(fs_section sec, std::span<uint8_t const> range,
                     std::shared_ptr<compression_progress> pctx)
      : type_{sec.type()}
      , compression_{sec.compression()}
      , range_{range}
      , pctx_{std::move(pctx)}
      , sec_{std::move(sec)} {}

  void
  compress(worker_group& wg, std::optional<std::string> /* meta */) override {
    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job([this, prom = std::move(prom)]() mutable {
      fsblock::build_section_header(header_, *this, sec_);
      if (pctx_) {
        pctx_->bytes_in += size();
        pctx_->bytes_out += size();
      }
      prom.set_value();
    });
  }

  void wait_until_compressed() override { future_.wait(); }

  section_type type() const override { return type_; }
  compression_type compression() const override { return compression_; }

  // TODO
  std::string description() const override { return "<compressed>"; }

  std::span<uint8_t const> data() const override { return range_; }

  size_t uncompressed_size() const override { return range_.size(); }
  size_t size() const override { return range_.size(); }
  size_t estimated_mem_usage() const override { return range_.size(); }

  void set_block_no(uint32_t number) override { number_ = number; }
  uint32_t block_no() const override { return number_.value(); }

  section_header_v2 const& header() const override { return header_; }

 private:
  section_type const type_;
  compression_type const compression_;
  std::span<uint8_t const> range_;
  std::future<void> future_;
  std::optional<uint32_t> number_;
  section_header_v2 header_;
  std::shared_ptr<compression_progress> pctx_;
  std::optional<fs_section> sec_;
};

class rewritten_fsblock : public fsblock::impl {
 public:
  rewritten_fsblock(section_type type, block_compressor const& bc,
                    delayed_data_fn_type data, size_t uncompressed_size,
                    std::shared_ptr<compression_progress> pctx)
      : type_{type}
      , bc_{bc}
      , data_{std::move(data)}
      , comp_type_{bc_.type()}
      , pctx_{std::move(pctx)}
      , uncompressed_size_{uncompressed_size} {
    DWARFS_CHECK(bc_, "block_compressor must not be null");
  }

  void compress(worker_group& wg, std::optional<std::string> meta) override {
    DWARFS_CHECK(!meta,
                 "metadata not supported for rewritten_fsblock::compress");

    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job([this, prom = std::move(prom)]() mutable {
      compress_job(std::move(prom));
    });
  }

  void wait_until_compressed() override { future_.get(); }

  section_type type() const override { return type_; }

  compression_type compression() const override { return comp_type_; }

  std::string description() const override { return bc_.describe(); }

  std::span<uint8_t const> data() const override {
    std::lock_guard lock(mx_);
    return block_data_.value().span();
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  size_t size() const override {
    std::lock_guard lock(mx_);
    DWARFS_CHECK(block_data_.has_value(), "block_data_ not set");
    return block_data_->size();
  }

  size_t estimated_mem_usage() const override {
    std::lock_guard lock(mx_);
    return block_data_.has_value() ? block_data_->capacity() : 0;
  }

  void set_block_no(uint32_t number) override {
    {
      std::lock_guard lock(mx_);
      DWARFS_CHECK(!number_.has_value(), "block number already set");
      number_ = number;
    }
  }

  uint32_t block_no() const override {
    std::lock_guard lock(mx_);
    return number_.value();
  }

  section_header_v2 const& header() const override {
    std::lock_guard lock(mx_);
    if (!header_) {
      header_ = section_header_v2{};
      fsblock::build_section_header(*header_, *this);
    }
    return header_.value();
  }

 private:
  void compress_job(std::promise<void> prom) {
    try {
      auto [block, meta] = data_();

      pctx_->bytes_in += block.size(); // TODO: data_.size()?

      try {
        if (meta) {
          block = bc_.compress(block, *meta);
        } else {
          block = bc_.compress(block);
        }
      } catch (bad_compression_ratio_error const&) {
        comp_type_ = compression_type::NONE;
      }

      pctx_->bytes_out += block.size();

      {
        std::lock_guard lock(mx_);
        block_data_.emplace(std::move(block));
      }

      prom.set_value();
    } catch (...) {
      prom.set_exception(std::current_exception());
    }
  }

  section_type const type_;
  block_compressor const& bc_;
  mutable std::recursive_mutex mx_;
  delayed_data_fn_type data_;
  std::optional<shared_byte_buffer> block_data_;
  std::future<void> future_;
  std::optional<uint32_t> number_;
  std::optional<section_header_v2> mutable header_;
  compression_type comp_type_;
  std::shared_ptr<compression_progress> pctx_;
  size_t const uncompressed_size_;
};

fsblock::fsblock(section_type type, block_compressor const& bc,
                 shared_byte_buffer data,
                 std::shared_ptr<compression_progress> pctx,
                 folly::Function<void(size_t)> set_block_cb)
    : impl_(std::make_unique<raw_fsblock>(type, bc, std::move(data),
                                          std::move(pctx),
                                          std::move(set_block_cb))) {}

fsblock::fsblock(section_type type, compression_type compression,
                 std::span<uint8_t const> data)
    : impl_(std::make_unique<compressed_fsblock>(type, compression, data)) {}

fsblock::fsblock(fs_section sec, std::span<uint8_t const> data,
                 std::shared_ptr<compression_progress> pctx)
    : impl_(std::make_unique<compressed_fsblock>(std::move(sec), data,
                                                 std::move(pctx))) {}

fsblock::fsblock(section_type type, block_compressor const& bc,
                 delayed_data_fn_type data, size_t uncompressed_size,
                 std::shared_ptr<compression_progress> pctx)
    : impl_(std::make_unique<rewritten_fsblock>(
          type, bc, std::move(data), uncompressed_size, std::move(pctx))) {}

void fsblock::build_section_header(section_header_v2& sh,
                                   fsblock::impl const& fsb,
                                   std::optional<fs_section> const& sec) {
  auto range = fsb.data();

  ::memcpy(sh.magic.data(), "DWARFS", 6);
  sh.major = MAJOR_VERSION;
  sh.minor = MINOR_VERSION;
  sh.number = fsb.block_no();
  sh.type = static_cast<uint16_t>(fsb.type());
  sh.compression = static_cast<uint16_t>(fsb.compression());
  sh.length = range.size();

  if (sec) {
    // This isn't just an optimization, it is actually a bit of a safety
    // feature. If we have an existing section header that we've previously
    // validated and we use its checksums, we can be sure that any mistake
    // in copying the data will be detected.

    auto secnum = sec->section_number();

    if (secnum && secnum.value() == sh.number) {
      auto xxh = sec->xxh3_64_value();
      auto sha = sec->sha2_512_256_value();

      if (xxh && sha && sha->size() == sh.sha2_512_256.size()) {
        sh.xxh3_64 = xxh.value();
        std::copy(sha->begin(), sha->end(), sh.sha2_512_256.data());
        return;
      }
    }
  }

  checksum xxh(checksum::xxh3_64);
  xxh.update(&sh.number,
             sizeof(section_header_v2) - offsetof(section_header_v2, number));
  xxh.update(range.data(), range.size());
  DWARFS_CHECK(xxh.finalize(&sh.xxh3_64), "XXH3-64 checksum failed");

  checksum sha(checksum::sha2_512_256);
  sha.update(&sh.xxh3_64,
             sizeof(section_header_v2) - offsetof(section_header_v2, xxh3_64));
  sha.update(range.data(), range.size());
  DWARFS_CHECK(sha.finalize(sh.sha2_512_256.data()),
               "SHA512/256 checksum failed");
}

} // namespace

template <typename LoggerPolicy>
class filesystem_writer_ final : public filesystem_writer_detail {
 public:
  using physical_block_cb_type =
      filesystem_writer_detail::physical_block_cb_type;

  filesystem_writer_(logger& lgr, std::ostream& os, worker_group& wg,
                     progress& prog, filesystem_writer_options const& options,
                     std::istream* header);
  ~filesystem_writer_() noexcept override;

  void add_default_compressor(block_compressor bc) override;
  void add_category_compressor(fragment_category::value_type cat,
                               block_compressor bc) override;
  void add_section_compressor(section_type type, block_compressor bc) override;
  compression_constraints
  get_compression_constraints(fragment_category::value_type cat,
                              std::string const& metadata) const override;
  block_compressor const& get_compressor(
      section_type type,
      std::optional<fragment_category::value_type> cat) const override;
  void configure(std::vector<fragment_category> const& expected_categories,
                 size_t max_active_slots) override;
  void configure_rewrite(size_t filesystem_size, size_t block_count) override;
  void copy_header(file_extents_iterable header) override;
  void write_block(fragment_category cat, shared_byte_buffer data,
                   physical_block_cb_type physical_block_cb,
                   std::optional<std::string> meta) override;
  void finish_category(fragment_category cat) override;
  void write_metadata_v2_schema(shared_byte_buffer data) override;
  void write_metadata_v2(shared_byte_buffer data) override;
  void write_history(shared_byte_buffer data) override;
  void check_block_compression(compression_type compression,
                               std::span<uint8_t const> data,
                               std::optional<fragment_category::value_type> cat,
                               std::optional<std::string> cat_metadata,
                               block_compression_info* info) override;
  void rewrite_section(section_type type, compression_type compression,
                       std::span<uint8_t const> data,
                       std::optional<fragment_category::value_type> cat,
                       std::optional<std::string> cat_metadata) override;
  void rewrite_block(delayed_data_fn_type data, size_t uncompressed_size,
                     std::optional<fragment_category::value_type> cat) override;
  void write_compressed_section(fs_section const& sec,
                                std::span<uint8_t const> data) override;
  void flush() override;
  size_t size() const override { return image_size_; }

 private:
  using block_merger_type =
      multi_queue_block_merger<fragment_category, std::unique_ptr<fsblock>,
                               fsblock_merger_policy>;
  using block_holder_type = block_merger_type::block_holder_type;

  block_compressor const&
  compressor_for_category(fragment_category::value_type cat) const;
  void
  write_block_impl(fragment_category cat, shared_byte_buffer data,
                   block_compressor const& bc, std::optional<std::string> meta,
                   physical_block_cb_type physical_block_cb);
  void rewrite_section_delayed_data(
      section_type type, delayed_data_fn_type data, size_t uncompressed_size,
      std::optional<fragment_category::value_type> cat);
  void on_block_merged(block_holder_type holder);
  void write_section_impl(section_type type, shared_byte_buffer data);
  void write(fsblock const& fsb);
  void write(char const* data, size_t size);
  template <typename T>
  void write(T const& obj);
  void write(std::span<uint8_t const> range);
  void writer_thread();
  void push_section_index(section_type type);
  void write_section_index();
  size_t mem_used() const;

  std::ostream& os_;
  size_t image_size_{0};
  std::istream* header_;
  worker_group& wg_;
  progress& prog_;
  std::optional<block_compressor> default_bc_;
  std::unordered_map<fragment_category::value_type, block_compressor>
      category_bc_;
  std::unordered_map<section_type, block_compressor> section_bc_;
  filesystem_writer_options const options_;
  LOG_PROXY_DECL(LoggerPolicy);
  std::deque<block_holder_type> queue_;
  std::shared_ptr<compression_progress> pctx_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  bool volatile flush_{true};
  std::thread writer_thread_;
  uint32_t section_number_{0};
  std::vector<uint64le_t> section_index_;
  std::ostream::pos_type header_size_{0};
  std::unique_ptr<block_merger_type> merger_;
};

// TODO: Maybe we can factor out the logic to find the right compressor
//       into something that gets passed a (section_type, category) pair?
template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::filesystem_writer_(
    logger& lgr, std::ostream& os, worker_group& wg, progress& prog,
    filesystem_writer_options const& options, std::istream* header)
    : os_(os)
    , header_(header)
    , wg_(wg)
    , prog_(prog)
    , options_(options)
    , LOG_PROXY_INIT(lgr) {
  if (header_) {
    if (options_.remove_header) {
      LOG_WARN << "header will not be written because remove_header is set";
    } else {
      image_size_ = header_size_ = copy_stream(*header_, os_);
    }
  }

  // TODO: the whole flush & thread thing needs to be revisited
  flush_ = false;
  writer_thread_ = std::thread(&filesystem_writer_::writer_thread, this);
}

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::~filesystem_writer_() noexcept {
  try {
    if (!flush_) {
      flush();
    }
  } catch (...) {
    DWARFS_PANIC(
        fmt::format("exception thrown in filesystem_writer destructor: {}",
                    exception_str(std::current_exception())));
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::writer_thread() {
  folly::setThreadName("writer");

  for (;;) {
    block_holder_type holder;

    {
      std::unique_lock lock(mx_);

      if (!flush_ and queue_.empty()) {
        cond_.wait(lock);
      }

      if (queue_.empty()) {
        if (flush_) {
          break;
        }
        continue;
      }

      std::swap(holder, queue_.front());
      queue_.pop_front();
    }

    cond_.notify_one();

    auto const& fsb = holder.value();

    // TODO: this may throw
    fsb->wait_until_compressed();

    LOG_DEBUG << get_friendly_section_name(fsb->type()) << " ["
              << fsb->block_no() << "] compressed from "
              << size_with_unit(fsb->uncompressed_size()) << " to "
              << size_with_unit(fsb->size()) << " [" << fsb->description()
              << "]";

    write(*fsb);
  }
}

template <typename LoggerPolicy>
size_t filesystem_writer_<LoggerPolicy>::mem_used() const {
  size_t s = 0;

  for (auto const& holder : queue_) {
    s += holder.value()->estimated_mem_usage();
  }

  LOG_VERBOSE << "mem_used: " << s;

  return s;
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(char const* data, size_t size) {
  // TODO: error handling :-)
  os_.write(data, size);
  image_size_ += size;
  prog_.compressed_size += size;
}

template <typename LoggerPolicy>
template <typename T>
void filesystem_writer_<LoggerPolicy>::write(T const& obj) {
  write(reinterpret_cast<char const*>(&obj), sizeof(T));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(std::span<uint8_t const> range) {
  write(reinterpret_cast<char const*>(range.data()), range.size());
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(fsblock const& fsb) {
  if (fsb.type() != section_type::SECTION_INDEX) {
    push_section_index(fsb.type());
  }

  write(fsb.header());
  write(fsb.data());

  if (fsb.type() == section_type::BLOCK) {
    prog_.blocks_written++;
  }
}

template <typename LoggerPolicy>
block_compressor const&
filesystem_writer_<LoggerPolicy>::compressor_for_category(
    fragment_category::value_type cat) const {
  if (auto it = category_bc_.find(cat); it != category_bc_.end()) {
    LOG_DEBUG << "using compressor (" << it->second.describe()
              << ") for category " << cat;
    return it->second;
  }
  LOG_DEBUG << "using default compressor (" << default_bc_.value().describe()
            << ") for category " << cat;
  return default_bc_.value();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_block_impl(
    fragment_category cat, shared_byte_buffer data, block_compressor const& bc,
    std::optional<std::string> meta, physical_block_cb_type physical_block_cb) {
  DWARFS_CHECK(merger_, "filesystem_writer not configured");

  std::shared_ptr<compression_progress> pctx;

  {
    std::unique_lock lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    pctx = pctx_;
  }

  LOG_DEBUG << "compressor memory usage: "
            << size_with_unit(bc.estimate_memory_usage(data.size()))
            << " (block size " << size_with_unit(data.size()) << ")";

  auto fsb = std::make_unique<fsblock>(section_type::BLOCK, bc, std::move(data),
                                       pctx, std::move(physical_block_cb));

  fsb->compress(wg_, std::move(meta));

  merger_->add(cat, std::move(fsb));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::on_block_merged(
    block_holder_type holder) {
  uint32_t number;

  {
    std::unique_lock lock(mx_);

    // TODO: move all section_number_ stuff to writer thread
    //       we probably can't do that if we want to build
    //       metadata in the background as we need to know
    //       the section numbers for that
    number = section_number_;
    holder.value()->set_block_no(section_number_++);

    queue_.emplace_back(std::move(holder));
  }

  LOG_DEBUG << "merged block " << number;

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::finish_category(fragment_category cat) {
  DWARFS_CHECK(merger_, "filesystem_writer not configured");
  merger_->finish(cat);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_section_impl(
    section_type type, shared_byte_buffer data) {
  auto& bc = get_compressor(type, std::nullopt);

  uint32_t number;

  {
    std::unique_lock lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    auto fsb = std::make_unique<fsblock>(type, bc, std::move(data), pctx_);

    number = section_number_;
    fsb->set_block_no(section_number_++);
    fsb->compress(wg_);

    queue_.emplace_back(std::move(fsb));
  }

  LOG_DEBUG << "write section " << number;

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::check_block_compression(
    compression_type compression, std::span<uint8_t const> data,
    std::optional<fragment_category::value_type> cat,
    std::optional<std::string> cat_metadata, block_compression_info* info) {
  block_compressor const* bc{nullptr};

  if (cat) {
    bc = &compressor_for_category(*cat);
  } else {
    bc = &default_bc_.value();
  }

  block_decompressor bd(compression, data);

  if (!cat_metadata) {
    cat_metadata = bd.metadata();
  }

  if (auto reqstr = bc->metadata_requirements(); !reqstr.empty()) {
    auto req = compression_metadata_requirements<nlohmann::json>{reqstr};

    try {
      req.check(cat_metadata);
    } catch (std::exception const& e) {
      auto msg = fmt::format(
          "cannot compress {} compressed block with compressor '{}' because "
          "the following metadata requirements are not met: {}",
          get_compression_name(compression), bc->describe(), e.what());
      DWARFS_THROW(runtime_error, msg);
    }
  }

  if (info) {
    info->uncompressed_size = bd.uncompressed_size();
    info->metadata = cat_metadata;
    if (info->metadata) {
      info->constraints = bc->get_compression_constraints(*info->metadata);
    }
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::rewrite_section_delayed_data(
    section_type type, delayed_data_fn_type data, size_t uncompressed_size,
    std::optional<fragment_category::value_type> cat) {
  {
    std::unique_lock lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    // TODO: this isn't currently working
    while (mem_used() > options_.max_queue_size) {
      LOG_VERBOSE << "waiting for queue to drain";
      cond_.wait(lock);
    }

    auto& bc = get_compressor(type, cat);

    auto fsb = std::make_unique<fsblock>(type, bc, std::move(data),
                                         uncompressed_size, pctx_);

    fsb->set_block_no(section_number_++);
    fsb->compress(wg_);

    queue_.emplace_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::rewrite_section(
    section_type type, compression_type compression,
    std::span<uint8_t const> data,
    std::optional<fragment_category::value_type> cat,
    std::optional<std::string> cat_metadata) {
  auto bd = block_decompressor(compression, data);
  auto uncompressed_size = bd.uncompressed_size();

  if (!cat_metadata) {
    cat_metadata = bd.metadata();
  }

  rewrite_section_delayed_data(
      type,
      [bd = std::move(bd), meta = std::move(cat_metadata)]() mutable {
        auto block = bd.start_decompression(malloc_byte_buffer::create());
        bd.decompress_frame(bd.uncompressed_size());
        return std::pair{std::move(block), meta};
      },
      uncompressed_size, cat);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::rewrite_block(
    delayed_data_fn_type data, size_t uncompressed_size,
    std::optional<fragment_category::value_type> cat) {
  rewrite_section_delayed_data(section_type::BLOCK, std::move(data),
                               uncompressed_size, cat);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_compressed_section(
    fs_section const& sec, std::span<uint8_t const> data) {
  {
    std::lock_guard lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    auto fsb = std::make_unique<fsblock>(sec, data, pctx_);

    fsb->set_block_no(section_number_++);
    fsb->compress(wg_);

    queue_.emplace_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::add_default_compressor(
    block_compressor bc) {
  DWARFS_CHECK(bc, "block_compressor must not be null");

  LOG_DEBUG << "adding default compressor (" << bc.describe() << ")";

  DWARFS_CHECK(!default_bc_, "default compressor registered more than once");

  default_bc_ = std::move(bc);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::add_category_compressor(
    fragment_category::value_type cat, block_compressor bc) {
  DWARFS_CHECK(bc, "block_compressor must not be null");

  LOG_DEBUG << "adding compressor (" << bc.describe() << ") for category "
            << cat;

  DWARFS_CHECK(
      category_bc_.emplace(cat, std::move(bc)).second,
      fmt::format("compressor registered more than once for category {}", cat));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::add_section_compressor(
    section_type type, block_compressor bc) {
  DWARFS_CHECK(bc, "block_compressor must not be null");

  LOG_DEBUG << "adding compressor (" << bc.describe() << ") for section type "
            << get_friendly_section_name(type);

  DWARFS_CHECK(type != section_type::SECTION_INDEX,
               "SECTION_INDEX is always uncompressed");

  if (auto reqstr = bc.metadata_requirements(); !reqstr.empty()) {
    try {
      auto req = compression_metadata_requirements<nlohmann::json>{reqstr};
      req.check(std::nullopt);
    } catch (std::exception const& e) {
      auto msg =
          fmt::format("cannot use '{}' for {} compression because compression "
                      "metadata requirements are not met: {}",
                      bc.describe(), get_friendly_section_name(type), e.what());
      DWARFS_THROW(runtime_error, msg);
    }
  }

  DWARFS_CHECK(
      section_bc_.emplace(type, std::move(bc)).second,
      fmt::format("compressor registered more than once for section type {}",
                  get_friendly_section_name(type)));
}

template <typename LoggerPolicy>
auto filesystem_writer_<LoggerPolicy>::get_compression_constraints(
    fragment_category::value_type cat, std::string const& metadata) const
    -> compression_constraints {
  return compressor_for_category(cat).get_compression_constraints(metadata);
}

template <typename LoggerPolicy>
block_compressor const& filesystem_writer_<LoggerPolicy>::get_compressor(
    section_type type, std::optional<fragment_category::value_type> cat) const {
  if (cat) {
    DWARFS_CHECK(type == section_type::BLOCK,
                 "category-specific compressors are only supported for blocks");
    return compressor_for_category(*cat);
  }

  if (auto it = section_bc_.find(type); it != section_bc_.end()) {
    return it->second;
  }

  return default_bc_.value();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::configure(
    std::vector<fragment_category> const& expected_categories,
    size_t max_active_slots) {
  DWARFS_CHECK(!merger_, "filesystem_writer already configured");

  merger_ = std::make_unique<block_merger_type>(
      max_active_slots, options_.max_queue_size, expected_categories,
      [this](auto&& holder) {
        on_block_merged(std::forward<decltype(holder)>(holder));
      },
      fsblock_merger_policy{options_.worst_case_block_size});
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::configure_rewrite(size_t filesystem_size,
                                                         size_t block_count) {
  prog_.original_size = filesystem_size;
  prog_.filesystem_size = filesystem_size;
  prog_.block_count = block_count;
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::copy_header(
    file_extents_iterable header) {
  if (!options_.remove_header) {
    if (header_) {
      LOG_WARN << "replacing old header";
    } else {
      for (auto const& ext : header) {
        for (auto const& seg : ext.segments()) {
          write(seg.span<uint8_t>());
        }
      }
      header_size_ = size();
    }
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_block(
    fragment_category cat, shared_byte_buffer data,
    physical_block_cb_type physical_block_cb, std::optional<std::string> meta) {
  write_block_impl(cat, std::move(data), compressor_for_category(cat.value()),
                   std::move(meta), std::move(physical_block_cb));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2_schema(
    shared_byte_buffer data) {
  write_section_impl(section_type::METADATA_V2_SCHEMA, std::move(data));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2(
    shared_byte_buffer data) {
  write_section_impl(section_type::METADATA_V2, std::move(data));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_history(shared_byte_buffer data) {
  write_section_impl(section_type::HISTORY, std::move(data));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::flush() {
  {
    std::lock_guard lock(mx_);

    if (flush_) {
      return;
    }

    flush_ = true;
  }

  cond_.notify_one();

  writer_thread_.join();

  if (!options_.no_section_index) {
    write_section_index();
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::push_section_index(section_type type) {
  section_index_.emplace_back((static_cast<uint64_t>(type) << 48) |
                              static_cast<uint64_t>(size() - header_size_));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_section_index() {
  push_section_index(section_type::SECTION_INDEX);
  auto data = std::span(reinterpret_cast<uint8_t*>(section_index_.data()),
                        sizeof(section_index_[0]) * section_index_.size());

  auto fsb = fsblock(section_type::SECTION_INDEX, compression_type::NONE, data);

  fsb.set_block_no(section_number_++);
  fsb.compress(wg_);
  fsb.wait_until_compressed();

  write(fsb);
}

} // namespace internal

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     thread_pool& pool, writer_progress& prog)
    : filesystem_writer(os, lgr, pool, prog, {}) {}

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     thread_pool& pool, writer_progress& prog,
                                     filesystem_writer_options const& options,
                                     std::istream* header)
    : impl_{make_unique_logging_object<internal::filesystem_writer_detail,
                                       internal::filesystem_writer_,
                                       logger_policies>(
          lgr, os, pool.get_worker_group(), prog.get_internal(), options,
          header)} {}

filesystem_writer::~filesystem_writer() noexcept = default;
filesystem_writer::filesystem_writer(filesystem_writer&&) noexcept = default;
filesystem_writer&
filesystem_writer::operator=(filesystem_writer&&) noexcept = default;

void filesystem_writer::add_default_compressor(block_compressor bc) {
  impl_->add_default_compressor(std::move(bc));
}

void filesystem_writer::add_category_compressor(
    fragment_category::value_type cat, block_compressor bc) {
  impl_->add_category_compressor(cat, std::move(bc));
}

void filesystem_writer::add_section_compressor(section_type type,
                                               block_compressor bc) {
  impl_->add_section_compressor(type, std::move(bc));
}

} // namespace dwarfs::writer
