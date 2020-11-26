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
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include <folly/system/ThreadName.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/progress.h"
#include "dwarfs/util.h"

namespace dwarfs {

class fsblock {
 private:
  class state {
   public:
    state(std::vector<uint8_t>&& data)
        : compressed_(false)
        , data_(std::move(data)) {}

    template <typename LogProxy>
    void compress(const block_compressor& bc, LogProxy& lp) {
      std::vector<uint8_t> tmp;

      {
        auto td = lp.timed_trace();

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
  };

 public:
  fsblock(section_type type, const block_compressor& bc,
          std::vector<uint8_t>&& data)
      : type_(type)
      , bc_(bc)
      , uncompressed_size_(data.size())
      , state_(std::make_shared<state>(std::move(data))) {}

  template <typename LogProxy>
  void compress(worker_group& wg, LogProxy& lp) {
    lp.trace() << "block queued for compression";

    std::shared_ptr<state> s = state_;

    wg.add_job([&, s] {
      lp.trace() << "block compression started";
      s->compress(bc_, lp);
    });
  }

  void wait_until_compressed() { state_->wait(); }

  section_type type() const { return type_; }

  compression_type compression() const { return bc_.type(); }

  const std::vector<uint8_t>& data() const {
    return state_->data();
    ;
  }

  size_t uncompressed_size() const { return uncompressed_size_; }

  size_t size() const { return state_->size(); }

 private:
  const section_type type_;
  block_compressor const& bc_;
  const size_t uncompressed_size_;
  std::shared_ptr<state> state_;
};

template <typename LoggerPolicy>
class filesystem_writer_ : public filesystem_writer::impl {
 public:
  filesystem_writer_(logger& lgr, std::ostream& os, worker_group& wg,
                     progress& prog, const block_compressor& bc,
                     const block_compressor& metadata_bc,
                     size_t max_queue_size);
  ~filesystem_writer_() noexcept;

  void write_block(std::vector<uint8_t>&& data) override;
  void write_metadata(std::vector<uint8_t>&& data) override;
  void write_metadata_v2(std::vector<uint8_t>&& data) override;
  void flush() override;
  size_t size() const override { return os_.tellp(); }

 private:
  void write_section(section_type type, std::vector<uint8_t>&& data,
                     block_compressor const& bc);
  void write(section_type type, compression_type compression,
             const std::vector<uint8_t>& data);
  void write(const char* data, size_t size);
  template <typename T>
  void write(const T& obj);
  void write(const std::vector<uint8_t>& data);
  void write_file_header();
  void writer_thread();
  size_t mem_used() const;

  std::ostream& os_;
  worker_group& wg_;
  progress& prog_;
  const block_compressor& bc_;
  const block_compressor& metadata_bc_;
  const size_t max_queue_size_;
  log_proxy<LoggerPolicy> log_;
  std::deque<std::unique_ptr<fsblock>> queue_;
  mutable std::mutex mx_;
  std::condition_variable cond_;
  volatile bool flush_;
  std::thread writer_thread_;
};

template <typename LoggerPolicy>
filesystem_writer_<LoggerPolicy>::filesystem_writer_(
    logger& lgr, std::ostream& os, worker_group& wg, progress& prog,
    const block_compressor& bc, const block_compressor& metadata_bc,
    size_t max_queue_size)
    : os_(os)
    , wg_(wg)
    , prog_(prog)
    , bc_(bc)
    , metadata_bc_(metadata_bc)
    , max_queue_size_(max_queue_size)
    , log_(lgr)
    , flush_(false)
    , writer_thread_(&filesystem_writer_::writer_thread, this) {
  write_file_header();
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

    log_.debug() << (fsb->type() == section_type::METADATA ? "metadata"
                                                           : "block")
                 << " compressed from "
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
void filesystem_writer_<LoggerPolicy>::write(const std::vector<uint8_t>& data) {
  write(reinterpret_cast<const char*>(&data[0]), data.size());
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write_file_header() {
  file_header hdr;
  ::memcpy(&hdr.magic[0], "DWARFS", 6);
  hdr.major = MAJOR_VERSION;
  hdr.minor = MINOR_VERSION;
  write(hdr);
}

template <typename LoggerPolicy>
void filesystem_writer_<LoggerPolicy>::write(section_type type,
                                             compression_type compression,
                                             const std::vector<uint8_t>& data) {
  section_header sh;
  sh.type = type;
  sh.compression = compression;
  sh.unused = 0;
  sh.length = data.size();
  write(sh);
  write(data);

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

  auto fsb = std::make_unique<fsblock>(type, bc, std::move(data));

  fsb->compress(wg_, log_);

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
void filesystem_writer_<LoggerPolicy>::write_metadata(
    std::vector<uint8_t>&& data) {
  write_section(section_type::METADATA, std::move(data), metadata_bc_);
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

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     size_t max_queue_size)
    : filesystem_writer(os, lgr, wg, prog, bc, bc, max_queue_size) {}

filesystem_writer::filesystem_writer(std::ostream& os, logger& lgr,
                                     worker_group& wg, progress& prog,
                                     const block_compressor& bc,
                                     const block_compressor& metadata_bc,
                                     size_t max_queue_size)
    : impl_(
          make_unique_logging_object<impl, filesystem_writer_, logger_policies>(
              lgr, os, wg, prog, bc, metadata_bc, max_queue_size)) {}

} // namespace dwarfs
