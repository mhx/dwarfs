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

#include <atomic>
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

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_data.h"
#include "dwarfs/checksum.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/multi_queue_block_merger.h"
#include "dwarfs/progress.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

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
          std::shared_ptr<block_data>&& data,
          std::shared_ptr<compression_progress> pctx,
          folly::Function<void(size_t)> set_block_cb = nullptr);

  fsblock(section_type type, compression_type compression,
          std::span<uint8_t const> data);

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
    virtual void set_block_no(uint32_t number) = 0;
    virtual uint32_t block_no() const = 0;
    virtual section_header_v2 const& header() const = 0;
  };

  static void
  build_section_header(section_header_v2& sh, fsblock::impl const& fsb);

 private:
  std::unique_ptr<impl> impl_;
};

class fsblock_merger_policy {
 public:
  fsblock_merger_policy(size_t worst_case_block_size)
      : worst_case_block_size_{worst_case_block_size} {}

  static size_t block_size(std::unique_ptr<fsblock> const& fsb) {
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
  raw_fsblock(section_type type, const block_compressor& bc,
              std::shared_ptr<block_data>&& data,
              std::shared_ptr<compression_progress> pctx,
              folly::Function<void(size_t)> set_block_cb)
      : type_{type}
      , bc_{bc}
      , uncompressed_size_{data->size()}
      , data_{std::move(data)}
      , comp_type_{bc_.type()}
      , pctx_{std::move(pctx)}
      , set_block_cb_{std::move(set_block_cb)} {}

  void compress(worker_group& wg, std::optional<std::string> meta) override {
    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job([this, prom = std::move(prom),
                meta = std::move(meta)]() mutable {
      try {
        std::shared_ptr<block_data> tmp;

        if (meta) {
          tmp = std::make_shared<block_data>(bc_.compress(data_->vec(), *meta));
        } else {
          tmp = std::make_shared<block_data>(bc_.compress(data_->vec()));
        }

        pctx_->bytes_in += data_->vec().size();
        pctx_->bytes_out += tmp->vec().size();

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

  std::span<uint8_t const> data() const override { return data_->vec(); }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  size_t size() const override {
    std::lock_guard lock(mx_);
    return data_->size();
  }

  void set_block_no(uint32_t number) override {
    {
      std::lock_guard lock(mx_);
      if (number_) {
        DWARFS_THROW(runtime_error, "block number already set");
      }
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
  const section_type type_;
  block_compressor const& bc_;
  const size_t uncompressed_size_;
  mutable std::recursive_mutex mx_;
  std::shared_ptr<block_data> data_;
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

  void
  compress(worker_group& wg, std::optional<std::string> /* meta */) override {
    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job([this, prom = std::move(prom)]() mutable {
      fsblock::build_section_header(header_, *this);
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
};

fsblock::fsblock(section_type type, block_compressor const& bc,
                 std::shared_ptr<block_data>&& data,
                 std::shared_ptr<compression_progress> pctx,
                 folly::Function<void(size_t)> set_block_cb)
    : impl_(std::make_unique<raw_fsblock>(type, bc, std::move(data),
                                          std::move(pctx),
                                          std::move(set_block_cb))) {}

fsblock::fsblock(section_type type, compression_type compression,
                 std::span<uint8_t const> data)
    : impl_(std::make_unique<compressed_fsblock>(type, compression, data)) {}

void fsblock::build_section_header(section_header_v2& sh,
                                   fsblock::impl const& fsb) {
  auto range = fsb.data();

  ::memcpy(&sh.magic[0], "DWARFS", 6);
  sh.major = MAJOR_VERSION;
  sh.minor = MINOR_VERSION;
  sh.number = fsb.block_no();
  sh.type = static_cast<uint16_t>(fsb.type());
  sh.compression = static_cast<uint16_t>(fsb.compression());
  sh.length = range.size();

  checksum xxh(checksum::algorithm::XXH3_64);
  xxh.update(&sh.number,
             sizeof(section_header_v2) - offsetof(section_header_v2, number));
  xxh.update(range.data(), range.size());
  DWARFS_CHECK(xxh.finalize(&sh.xxh3_64), "XXH3-64 checksum failed");

  checksum sha(checksum::algorithm::SHA2_512_256);
  sha.update(&sh.xxh3_64,
             sizeof(section_header_v2) - offsetof(section_header_v2, xxh3_64));
  sha.update(range.data(), range.size());
  DWARFS_CHECK(sha.finalize(&sh.sha2_512_256), "SHA512/256 checksum failed");
}

template <typename LoggerPolicy>
class filesystem_writer_ final : public filesystem_writer::impl {
 public:
  using physical_block_cb_type = filesystem_writer::physical_block_cb_type;

  filesystem_writer_(logger& lgr, std::ostream& os, worker_group& wg,
                     progress& prog, const block_compressor& schema_bc,
                     const block_compressor& metadata_bc,
                     filesystem_writer_options const& options,
                     std::istream* header);
  ~filesystem_writer_() noexcept override;

  void add_default_compressor(block_compressor bc) override;
  void add_category_compressor(fragment_category::value_type cat,
                               block_compressor bc) override;
  compression_constraints
  get_compression_constraints(fragment_category::value_type cat,
                              std::string const& metadata) const override;
  void configure(std::vector<fragment_category> const& expected_categories,
                 size_t max_active_slots) override;
  void copy_header(std::span<uint8_t const> header) override;
  void write_block(fragment_category cat, std::shared_ptr<block_data>&& data,
                   physical_block_cb_type physical_block_cb,
                   std::optional<std::string> meta) override;
  void finish_category(fragment_category cat) override;
  void write_block(fragment_category::value_type cat,
                   std::shared_ptr<block_data>&& data,
                   std::optional<std::string> meta) override;
  void write_metadata_v2_schema(std::shared_ptr<block_data>&& data) override;
  void write_metadata_v2(std::shared_ptr<block_data>&& data) override;
  void write_compressed_section(section_type type, compression_type compression,
                                std::span<uint8_t const> data) override;
  void flush() override;
  size_t size() const override { return os_.tellp(); }

 private:
  using block_merger_type =
      multi_queue_block_merger<fragment_category, std::unique_ptr<fsblock>,
                               fsblock_merger_policy>;
  using block_holder_type = block_merger_type::block_holder_type;

  block_compressor const&
  compressor_for_category(fragment_category::value_type cat) const;
  void
  write_block_impl(fragment_category cat, std::shared_ptr<block_data>&& data,
                   block_compressor const& bc, std::optional<std::string> meta,
                   physical_block_cb_type physical_block_cb);
  void on_block_merged(block_holder_type holder);
  void write_section(section_type type, std::shared_ptr<block_data>&& data,
                     block_compressor const& bc,
                     std::optional<std::string> meta = std::nullopt);
  void write(fsblock const& fsb);
  void write(const char* data, size_t size);
  template <typename T>
  void write(const T& obj);
  void write(std::span<uint8_t const> range);
  void writer_thread();
  void push_section_index(section_type type);
  void write_section_index();
  size_t mem_used() const;

  std::ostream& os_;
  std::istream* header_;
  worker_group& wg_;
  progress& prog_;
  std::optional<block_compressor> default_bc_;
  std::unordered_map<fragment_category::value_type, block_compressor> bc_;
  const block_compressor& schema_bc_;
  const block_compressor& metadata_bc_;
  const filesystem_writer_options options_;
  LOG_PROXY_DECL(LoggerPolicy);
  std::deque<block_holder_type> queue_;
  std::shared_ptr<compression_progress> pctx_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  volatile bool flush_;
  std::thread writer_thread_;
  uint32_t section_number_{0};
  std::vector<uint64_t> section_index_;
  std::ostream::pos_type header_size_{0};
  std::unique_ptr<block_merger_type> merger_;
};

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::filesystem_writer_(
    logger& lgr, std::ostream& os, worker_group& wg, progress& prog,
    const block_compressor& schema_bc, const block_compressor& metadata_bc,
    filesystem_writer_options const& options, std::istream* header)
    : os_(os)
    , header_(header)
    , wg_(wg)
    , prog_(prog)
    , schema_bc_(schema_bc)
    , metadata_bc_(metadata_bc)
    , options_(options)
    , LOG_PROXY_INIT(lgr)
    , flush_(false)
    , writer_thread_(&filesystem_writer_::writer_thread, this) {
  if (header_) {
    if (options_.remove_header) {
      LOG_WARN << "header will not be written because remove_header is set";
    } else {
      os_ << header_->rdbuf();
      header_size_ = os_.tellp();
    }
  }
}

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::~filesystem_writer_() noexcept {
  try {
    if (!flush_) {
      flush();
    }
  } catch (...) {
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
        if (flush_)
          break;
        else
          continue;
      }

      std::swap(holder, queue_.front());
      queue_.pop_front();
    }

    cond_.notify_one();

    auto const& fsb = holder.value();

    fsb->wait_until_compressed();

    LOG_DEBUG << get_section_name(fsb->type()) << " [" << fsb->block_no()
              << "] compressed from "
              << size_with_unit(fsb->uncompressed_size()) << " to "
              << size_with_unit(fsb->size()) << " [" << fsb->description()
              << "]";

    write(*fsb);
  }
}

template <typename LoggerPolicy>
size_t filesystem_writer_<LoggerPolicy>::mem_used() const {
  size_t s = 0;

  for (const auto& holder : queue_) {
    s += holder.value()->size();
  }

  return s;
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(const char* data, size_t size) {
  // TODO: error handling :-)
  os_.write(data, size);
  prog_.compressed_size += size;
}

template <typename LoggerPolicy>
template <typename T>
void filesystem_writer_<LoggerPolicy>::write(const T& obj) {
  write(reinterpret_cast<const char*>(&obj), sizeof(T));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(std::span<uint8_t const> range) {
  write(reinterpret_cast<const char*>(range.data()), range.size());
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
  if (auto it = bc_.find(cat); it != bc_.end()) {
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
    fragment_category cat, std::shared_ptr<block_data>&& data,
    block_compressor const& bc, std::optional<std::string> meta,
    physical_block_cb_type physical_block_cb) {
  if (!merger_) {
    DWARFS_THROW(runtime_error, "filesystem_writer not configured");
  }

  std::shared_ptr<compression_progress> pctx;

  {
    std::unique_lock lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    pctx = pctx_;
  }

  auto fsb = std::make_unique<fsblock>(section_type::BLOCK, bc, std::move(data),
                                       pctx, std::move(physical_block_cb));

  fsb->compress(wg_, meta);

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
  if (!merger_) {
    DWARFS_THROW(runtime_error, "filesystem_writer not configured");
  }

  merger_->finish(cat);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_section(
    section_type type, std::shared_ptr<block_data>&& data,
    block_compressor const& bc, std::optional<std::string> meta) {
  uint32_t number;

  {
    std::unique_lock lock(mx_);

    if (!pctx_) {
      pctx_ = prog_.create_context<compression_progress>();
    }

    while (mem_used() > options_.max_queue_size) {
      cond_.wait(lock);
    }

    auto fsb = std::make_unique<fsblock>(type, bc, std::move(data), pctx_);

    number = section_number_;
    fsb->set_block_no(section_number_++);
    fsb->compress(wg_, meta);

    queue_.emplace_back(std::move(fsb));
  }

  LOG_DEBUG << "write section " << number;

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_compressed_section(
    section_type type, compression_type compression,
    std::span<uint8_t const> data) {
  {
    std::lock_guard lock(mx_);

    auto fsb = std::make_unique<fsblock>(type, compression, data);

    fsb->set_block_no(section_number_++);
    fsb->compress(wg_);

    queue_.emplace_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::add_default_compressor(
    block_compressor bc) {
  if (default_bc_) {
    DWARFS_THROW(runtime_error, "default compressor registered more than once");
  }
  default_bc_ = std::move(bc);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::add_category_compressor(
    fragment_category::value_type cat, block_compressor bc) {
  LOG_DEBUG << "adding compressor (" << bc.describe() << ") for category "
            << cat;
  if (!bc_.emplace(cat, std::move(bc)).second) {
    DWARFS_THROW(runtime_error,
                 "category compressor registered more than once");
  }
}

template <typename LoggerPolicy>
auto filesystem_writer_<LoggerPolicy>::get_compression_constraints(
    fragment_category::value_type cat, std::string const& metadata) const
    -> compression_constraints {
  return compressor_for_category(cat).get_compression_constraints(metadata);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::configure(
    std::vector<fragment_category> const& expected_categories,
    size_t max_active_slots) {
  if (merger_) {
    DWARFS_THROW(runtime_error, "filesystem_writer already configured");
  }

  merger_ = std::make_unique<block_merger_type>(
      max_active_slots, options_.max_queue_size, expected_categories,
      [this](auto&& holder) { on_block_merged(std::move(holder)); },
      fsblock_merger_policy{options_.worst_case_block_size});
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::copy_header(
    std::span<uint8_t const> header) {
  if (!options_.remove_header) {
    if (header_) {
      LOG_WARN << "replacing old header";
    } else {
      write(header);
      header_size_ = os_.tellp();
    }
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_block(
    fragment_category cat, std::shared_ptr<block_data>&& data,
    physical_block_cb_type physical_block_cb, std::optional<std::string> meta) {
  write_block_impl(cat, std::move(data), compressor_for_category(cat.value()),
                   std::move(meta), std::move(physical_block_cb));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_block(
    fragment_category::value_type cat, std::shared_ptr<block_data>&& data,
    std::optional<std::string> meta) {
  write_section(section_type::BLOCK, std::move(data),
                compressor_for_category(cat), std::move(meta));
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2_schema(
    std::shared_ptr<block_data>&& data) {
  write_section(section_type::METADATA_V2_SCHEMA, std::move(data), schema_bc_);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2(
    std::shared_ptr<block_data>&& data) {
  write_section(section_type::METADATA_V2, std::move(data), metadata_bc_);
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
  section_index_.push_back((static_cast<uint64_t>(type) << 48) |
                           static_cast<uint64_t>(os_.tellp() - header_size_));
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

} // namespace

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& schema_bc,
                                     const block_compressor& metadata_bc,
                                     filesystem_writer_options const& options,
                                     std::istream* header)
    : impl_(
          make_unique_logging_object<impl, filesystem_writer_, logger_policies>(
              lgr, os, wg, prog, schema_bc, metadata_bc, options, header)) {}

} // namespace dwarfs
