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
#include <thread>

#include <folly/Range.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/block_data.h"
#include "dwarfs/checksum.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/progress.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

class fsblock {
 public:
  fsblock(section_type type, block_compressor const& bc,
          std::shared_ptr<block_data>&& data, uint32_t number);

  fsblock(section_type type, compression_type compression,
          folly::ByteRange data, uint32_t number);

  void compress(worker_group& wg) { impl_->compress(wg); }
  void wait_until_compressed() { impl_->wait_until_compressed(); }
  section_type type() const { return impl_->type(); }
  compression_type compression() const { return impl_->compression(); }
  folly::ByteRange data() const { return impl_->data(); }
  size_t uncompressed_size() const { return impl_->uncompressed_size(); }
  size_t size() const { return impl_->size(); }
  uint32_t number() const { return impl_->number(); }
  section_header_v2 const& header() const { return impl_->header(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void compress(worker_group& wg) = 0;
    virtual void wait_until_compressed() = 0;
    virtual section_type type() const = 0;
    virtual compression_type compression() const = 0;
    virtual folly::ByteRange data() const = 0;
    virtual size_t uncompressed_size() const = 0;
    virtual size_t size() const = 0;
    virtual uint32_t number() const = 0;
    virtual section_header_v2 const& header() const = 0;
  };

  static void
  build_section_header(section_header_v2& sh, fsblock::impl const& fsb);

 private:
  std::unique_ptr<impl> impl_;
};

class raw_fsblock : public fsblock::impl {
 public:
  raw_fsblock(section_type type, const block_compressor& bc,
              std::shared_ptr<block_data>&& data, uint32_t number)
      : type_{type}
      , bc_{bc}
      , uncompressed_size_{data->size()}
      , data_{std::move(data)}
      , number_{number}
      , comp_type_{bc_.type()} {}

  void compress(worker_group& wg) override {
    std::promise<void> prom;
    future_ = prom.get_future();

    wg.add_job([this, prom = std::move(prom)]() mutable {
      try {
        auto tmp = std::make_shared<block_data>(bc_.compress(data_->vec()));

        {
          std::lock_guard lock(mx_);
          data_.swap(tmp);
        }
      } catch (bad_compression_ratio_error const& e) {
        comp_type_ = compression_type::NONE;
      }

      fsblock::build_section_header(header_, *this);

      prom.set_value();
    });
  }

  void wait_until_compressed() override { future_.wait(); }

  section_type type() const override { return type_; }

  compression_type compression() const override { return comp_type_; }

  folly::ByteRange data() const override { return data_->vec(); }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  size_t size() const override {
    std::lock_guard lock(mx_);
    return data_->size();
  }

  uint32_t number() const override { return number_; }

  section_header_v2 const& header() const override { return header_; }

 private:
  const section_type type_;
  block_compressor const& bc_;
  const size_t uncompressed_size_;
  mutable std::mutex mx_;
  std::shared_ptr<block_data> data_;
  std::future<void> future_;
  uint32_t const number_;
  section_header_v2 header_;
  compression_type comp_type_;
};

class compressed_fsblock : public fsblock::impl {
 public:
  compressed_fsblock(section_type type, compression_type compression,
                     folly::ByteRange range, uint32_t number)
      : type_{type}
      , compression_{compression}
      , range_{range}
      , number_{number} {}

  void compress(worker_group& wg) override {
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

  folly::ByteRange data() const override { return range_; }

  size_t uncompressed_size() const override { return range_.size(); }
  size_t size() const override { return range_.size(); }

  uint32_t number() const override { return number_; }

  section_header_v2 const& header() const override { return header_; }

 private:
  section_type const type_;
  compression_type const compression_;
  folly::ByteRange range_;
  std::future<void> future_;
  uint32_t const number_;
  section_header_v2 header_;
};

fsblock::fsblock(section_type type, block_compressor const& bc,
                 std::shared_ptr<block_data>&& data, uint32_t number)
    : impl_(std::make_unique<raw_fsblock>(type, bc, std::move(data), number)) {}

fsblock::fsblock(section_type type, compression_type compression,
                 folly::ByteRange data, uint32_t number)
    : impl_(std::make_unique<compressed_fsblock>(type, compression, data,
                                                 number)) {}

void fsblock::build_section_header(section_header_v2& sh,
                                   fsblock::impl const& fsb) {
  auto range = fsb.data();

  ::memcpy(&sh.magic[0], "DWARFS", 6);
  sh.major = MAJOR_VERSION;
  sh.minor = MINOR_VERSION;
  sh.number = fsb.number();
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
  filesystem_writer_(logger& lgr, std::ostream& os, worker_group& wg,
                     progress& prog, const block_compressor& bc,
                     const block_compressor& schema_bc,
                     const block_compressor& metadata_bc,
                     filesystem_writer_options const& options,
                     std::istream* header);
  ~filesystem_writer_() noexcept override;

  void copy_header(folly::ByteRange header) override;
  void write_block(std::shared_ptr<block_data>&& data) override;
  void write_metadata_v2_schema(std::shared_ptr<block_data>&& data) override;
  void write_metadata_v2(std::shared_ptr<block_data>&& data) override;
  void write_compressed_section(section_type type, compression_type compression,
                                folly::ByteRange data) override;
  void flush() override;
  size_t size() const override { return os_.tellp(); }
  int queue_fill() const override { return static_cast<int>(wg_.queue_size()); }

 private:
  void write_section(section_type type, std::shared_ptr<block_data>&& data,
                     block_compressor const& bc);
  void write(fsblock const& fsb);
  void write(const char* data, size_t size);
  template <typename T>
  void write(const T& obj);
  void write(folly::ByteRange range);
  void writer_thread();
  void push_section_index(section_type type);
  void write_section_index();
  size_t mem_used() const;

  std::ostream& os_;
  std::istream* header_;
  worker_group& wg_;
  progress& prog_;
  const block_compressor& bc_;
  const block_compressor& schema_bc_;
  const block_compressor& metadata_bc_;
  const filesystem_writer_options options_;
  LOG_PROXY_DECL(LoggerPolicy);
  std::deque<std::unique_ptr<fsblock>> queue_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  volatile bool flush_;
  std::thread writer_thread_;
  uint32_t section_number_{0};
  std::vector<uint64_t> section_index_;
  std::ostream::pos_type header_size_{0};
};

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::filesystem_writer_(
    logger& lgr, std::ostream& os, worker_group& wg, progress& prog,
    const block_compressor& bc, const block_compressor& schema_bc,
    const block_compressor& metadata_bc,
    filesystem_writer_options const& options, std::istream* header)
    : os_(os)
    , header_(header)
    , wg_(wg)
    , prog_(prog)
    , bc_(bc)
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
    std::unique_ptr<fsblock> fsb;

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

      fsb.swap(queue_.front());
      queue_.pop_front();
    }

    cond_.notify_one();

    fsb->wait_until_compressed();

    LOG_DEBUG << get_section_name(fsb->type()) << " compressed from "
              << size_with_unit(fsb->uncompressed_size()) << " to "
              << size_with_unit(fsb->size());

    write(*fsb);
  }
}

template <typename LoggerPolicy>
size_t filesystem_writer_<LoggerPolicy>::mem_used() const {
  size_t s = 0;

  for (const auto& fsb : queue_) {
    s += fsb->size();
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
void filesystem_writer_<LoggerPolicy>::write(folly::ByteRange range) {
  write(reinterpret_cast<const char*>(range.data()), range.size());
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(fsblock const& fsb) {
  push_section_index(fsb.type());

  write(fsb.header());
  write(fsb.data());

  if (fsb.type() == section_type::BLOCK) {
    prog_.blocks_written++;
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_section(
    section_type type, std::shared_ptr<block_data>&& data,
    block_compressor const& bc) {
  {
    std::unique_lock lock(mx_);

    while (mem_used() > options_.max_queue_size) {
      cond_.wait(lock);
    }
  }

  auto fsb =
      std::make_unique<fsblock>(type, bc, std::move(data), section_number_++);

  fsb->compress(wg_);

  {
    std::lock_guard lock(mx_);
    queue_.push_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_compressed_section(
    section_type type, compression_type compression, folly::ByteRange data) {
  auto fsb =
      std::make_unique<fsblock>(type, compression, data, section_number_++);

  fsb->compress(wg_);

  {
    std::lock_guard lock(mx_);
    queue_.push_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::copy_header(folly::ByteRange header) {
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
    std::shared_ptr<block_data>&& data) {
  write_section(section_type::BLOCK, std::move(data), bc_);
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
  auto data =
      folly::ByteRange(reinterpret_cast<uint8_t*>(section_index_.data()),
                       sizeof(section_index_[0]) * section_index_.size());

  auto fsb = fsblock(section_type::SECTION_INDEX, compression_type::NONE, data,
                     section_number_++);

  fsb.compress(wg_);
  fsb.wait_until_compressed();

  write(fsb);
}

} // namespace

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     filesystem_writer_options const& options,
                                     std::istream* header)
    : filesystem_writer(os, lgr, wg, prog, bc, bc, bc, options, header) {}

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     const block_compressor& schema_bc,
                                     const block_compressor& metadata_bc,
                                     filesystem_writer_options const& options,
                                     std::istream* header)
    : impl_(
          make_unique_logging_object<impl, filesystem_writer_, logger_policies>(
              lgr, os, wg, prog, bc, schema_bc, metadata_bc, options, header)) {
}

} // namespace dwarfs
