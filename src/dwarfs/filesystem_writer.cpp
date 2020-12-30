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
  fsblock(logger& lgr, section_type type, const block_compressor& bc,
          std::vector<uint8_t>&& data);

  fsblock(section_type type, compression_type compression,
          folly::ByteRange data);

  void compress(worker_group& wg) { impl_->compress(wg); }
  void wait_until_compressed() { impl_->wait_until_compressed(); }
  section_type type() const { return impl_->type(); }
  compression_type compression() const { return impl_->compression(); }
  folly::ByteRange data() const { return impl_->data(); }
  size_t uncompressed_size() const { return impl_->uncompressed_size(); }
  size_t size() const { return impl_->size(); }

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
  };

 private:
  std::unique_ptr<impl> impl_;
};

template <typename LoggerPolicy>
class raw_fsblock : public fsblock::impl {
 private:
  class state {
   public:
    state(std::vector<uint8_t>&& data, logger& lgr)
        : compressed_(false)
        , data_(std::move(data))
        , LOG_PROXY_INIT(lgr) {}

    void compress(const block_compressor& bc) {
      std::vector<uint8_t> tmp;

      {
        auto td = LOG_TIMED_TRACE;

        tmp = bc.compress(data_);

        td << "block compression finished";
      }

      {
        std::lock_guard<std::mutex> lock(mx_);
        data_.swap(tmp);
        compressed_ = true;
      }

      cond_.notify_one();
    }

    void wait() {
      std::unique_lock<std::mutex> lock(mx_);
      cond_.wait(lock, [&]() -> bool { return compressed_; });
    }

    const std::vector<uint8_t>& data() const { return data_; }

    size_t size() const {
      std::lock_guard<std::mutex> lock(mx_);
      return data_.size();
    }

   private:
    mutable std::mutex mx_;
    std::condition_variable cond_;
    std::atomic<bool> compressed_;
    std::vector<uint8_t> data_;
    LOG_PROXY_DECL(LoggerPolicy);
  };

 public:
  raw_fsblock(logger& lgr, section_type type, const block_compressor& bc,
              std::vector<uint8_t>&& data)
      : type_(type)
      , bc_(bc)
      , uncompressed_size_(data.size())
      , state_(std::make_shared<state>(std::move(data), lgr))
      , LOG_PROXY_INIT(lgr) {}

  void compress(worker_group& wg) override {
    LOG_TRACE << "block queued for compression";

    std::shared_ptr<state> s = state_;

    wg.add_job([&, s] {
      LOG_TRACE << "block compression started";
      s->compress(bc_);
    });
  }

  void wait_until_compressed() override { state_->wait(); }

  section_type type() const override { return type_; }

  compression_type compression() const override { return bc_.type(); }

  folly::ByteRange data() const override { return state_->data(); }

  size_t uncompressed_size() const override { return uncompressed_size_; }

  size_t size() const override { return state_->size(); }

 private:
  const section_type type_;
  block_compressor const& bc_;
  const size_t uncompressed_size_;
  std::shared_ptr<state> state_;
  LOG_PROXY_DECL(LoggerPolicy);
};

class compressed_fsblock : public fsblock::impl {
 public:
  compressed_fsblock(section_type type, compression_type compression,
                     folly::ByteRange range)
      : type_(type)
      , compression_(compression)
      , range_(range) {}

  void compress(worker_group&) override {}
  void wait_until_compressed() override {}

  section_type type() const override { return type_; }
  compression_type compression() const override { return compression_; }

  folly::ByteRange data() const override { return range_; }

  size_t uncompressed_size() const override { return range_.size(); }
  size_t size() const override { return range_.size(); }

 private:
  const section_type type_;
  const compression_type compression_;
  folly::ByteRange range_;
};

fsblock::fsblock(logger& lgr, section_type type, const block_compressor& bc,
                 std::vector<uint8_t>&& data)
    : impl_(make_unique_logging_object<impl, raw_fsblock, logger_policies>(
          lgr, type, bc, std::move(data))) {}

fsblock::fsblock(section_type type, compression_type compression,
                 folly::ByteRange data)
    : impl_(std::make_unique<compressed_fsblock>(type, compression, data)) {}

template <typename LoggerPolicy>
class filesystem_writer_ : public filesystem_writer::impl {
 public:
  filesystem_writer_(logger& lgr, std::ostream& os, worker_group& wg,
                     progress& prog, const block_compressor& bc,
                     const block_compressor& schema_bc,
                     const block_compressor& metadata_bc,
                     size_t max_queue_size);
  ~filesystem_writer_() noexcept;

  void write_block(std::vector<uint8_t>&& data) override;
  void write_metadata_v2_schema(std::vector<uint8_t>&& data) override;
  void write_metadata_v2(std::vector<uint8_t>&& data) override;
  void write_compressed_section(section_type type, compression_type compression,
                                folly::ByteRange data) override;
  void flush() override;
  size_t size() const override { return os_.tellp(); }
  int queue_fill() const override { return static_cast<int>(wg_.queue_size()); }

 private:
  void write_section(section_type type, std::vector<uint8_t>&& data,
                     block_compressor const& bc);
  void write(section_type type, compression_type compression,
             folly::ByteRange range);
  void write(const char* data, size_t size);
  template <typename T>
  void write(const T& obj);
  void write(folly::ByteRange range);
  void writer_thread();
  size_t mem_used() const;

  std::ostream& os_;
  worker_group& wg_;
  progress& prog_;
  const block_compressor& bc_;
  const block_compressor& schema_bc_;
  const block_compressor& metadata_bc_;
  const size_t max_queue_size_;
  LOG_PROXY_DECL(LoggerPolicy);
  std::deque<std::unique_ptr<fsblock>> queue_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  volatile bool flush_;
  std::thread writer_thread_;
  uint32_t section_number_{0};
};

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::filesystem_writer_(
    logger& lgr, std::ostream& os, worker_group& wg, progress& prog,
    const block_compressor& bc, const block_compressor& schema_bc,
    const block_compressor& metadata_bc, size_t max_queue_size)
    : os_(os)
    , wg_(wg)
    , prog_(prog)
    , bc_(bc)
    , schema_bc_(schema_bc)
    , metadata_bc_(metadata_bc)
    , max_queue_size_(max_queue_size)
    , LOG_PROXY_INIT(lgr)
    , flush_(false)
    , writer_thread_(&filesystem_writer_::writer_thread, this) {}

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
      std::unique_lock<std::mutex> lock(mx_);

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

    write(fsb->type(), fsb->compression(), fsb->data());
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
void filesystem_writer_<LoggerPolicy>::write(section_type type,
                                             compression_type compression,
                                             folly::ByteRange range) {
  section_header_v2 sh;
  ::memcpy(&sh.magic[0], "DWARFS", 6);
  sh.major = MAJOR_VERSION;
  sh.minor = MINOR_VERSION;
  sh.number = section_number_++;
  sh.type = static_cast<uint16_t>(type);
  sh.compression = static_cast<uint16_t>(compression);
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

  write(sh);
  write(range);

  if (type == section_type::BLOCK) {
    prog_.blocks_written++;
  }
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_section(
    section_type type, std::vector<uint8_t>&& data,
    block_compressor const& bc) {
  {
    std::unique_lock<std::mutex> lock(mx_);

    while (mem_used() > max_queue_size_) {
      cond_.wait(lock);
    }
  }

  auto fsb =
      std::make_unique<fsblock>(LOG_GET_LOGGER, type, bc, std::move(data));

  fsb->compress(wg_);

  {
    std::lock_guard<std::mutex> lock(mx_);
    queue_.push_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_compressed_section(
    section_type type, compression_type compression, folly::ByteRange data) {
  auto fsb = std::make_unique<fsblock>(type, compression, data);

  {
    std::lock_guard<std::mutex> lock(mx_);
    queue_.push_back(std::move(fsb));
  }

  cond_.notify_one();
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_block(
    std::vector<uint8_t>&& data) {
  write_section(section_type::BLOCK, std::move(data), bc_);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2_schema(
    std::vector<uint8_t>&& data) {
  write_section(section_type::METADATA_V2_SCHEMA, std::move(data), schema_bc_);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_metadata_v2(
    std::vector<uint8_t>&& data) {
  write_section(section_type::METADATA_V2, std::move(data), metadata_bc_);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::flush() {
  {
    std::lock_guard<std::mutex> lock(mx_);

    if (flush_) {
      return;
    }

    flush_ = true;
  }

  cond_.notify_one();

  writer_thread_.join();
}

} // namespace

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     size_t max_queue_size)
    : filesystem_writer(os, lgr, wg, prog, bc, bc, bc, max_queue_size) {}

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     const block_compressor& schema_bc,
                                     const block_compressor& metadata_bc,
                                     size_t max_queue_size)
    : impl_(
          make_unique_logging_object<impl, filesystem_writer_, logger_policies>(
              lgr, os, wg, prog, bc, schema_bc, metadata_bc, max_queue_size)) {}

} // namespace dwarfs
