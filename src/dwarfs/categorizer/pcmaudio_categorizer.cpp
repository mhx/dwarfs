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

#include <array>
#include <cassert>
#include <cstring>
#include <map>
#include <stack>
#include <unordered_set>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <folly/Synchronized.h>
#include <folly/lang/Bits.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

constexpr std::string_view const METADATA_CATEGORY{"metadata"};
constexpr std::string_view const PCMAUDIO_CATEGORY{"pcmaudio"};

constexpr size_t const MIN_PCMAUDIO_SIZE{64};

enum class endianness : uint8_t {
  BIG,
  LITTLE,
};

enum class signedness : uint8_t {
  SIGNED,
  UNSIGNED,
};

enum class padding : uint8_t {
  LSB,
  MSB,
};

char const* endianness_string(endianness e) {
  switch (e) {
  case endianness::BIG:
    return "big";
  case endianness::LITTLE:
    return "little";
  }
}

char const* signedness_string(signedness s) {
  switch (s) {
  case signedness::SIGNED:
    return "signed";
  case signedness::UNSIGNED:
    return "unsigned";
  }
}

char const* padding_string(padding p) {
  switch (p) {
  case padding::LSB:
    return "lsb";
  case padding::MSB:
    return "msb";
  }
}

struct pcmaudio_metadata {
  endianness sample_endianness;
  signedness sample_signedness;
  padding sample_padding;
  uint8_t bits_per_sample;
  uint8_t bytes_per_sample;
  uint16_t number_of_channels;

  //// Sample rate should be irrelevant
  // uint32_t samples_per_second;

  auto operator<=>(pcmaudio_metadata const&) const = default;
};

std::ostream& operator<<(std::ostream& os, pcmaudio_metadata const& m) {
  os << "[" << endianness_string(m.sample_endianness) << ", "
     << signedness_string(m.sample_signedness) << ", "
     << padding_string(m.sample_padding) << ", "
     << "bits=" << static_cast<int>(m.bits_per_sample) << ", "
     << "bytes=" << static_cast<int>(m.bytes_per_sample) << ", "
     << "channels=" << static_cast<int>(m.number_of_channels) << "]";
  return os;
}

class pcmaudio_metadata_store {
 public:
  pcmaudio_metadata_store() = default;

  size_t add(pcmaudio_metadata const& m) {
    auto it = reverse_index_.find(m);
    if (it == reverse_index_.end()) {
      auto r = reverse_index_.emplace(m, forward_index_.size());
      assert(r.second);
      forward_index_.emplace_back(m);
      it = r.first;
    }
    return it->second;
  }

  folly::dynamic lookup(size_t ix) const {
    auto const& m = DWARFS_NOTHROW(forward_index_.at(ix));
    folly::dynamic obj = folly::dynamic::object;
    obj.insert("endianness", endianness_string(m.sample_endianness));
    obj.insert("signedness", signedness_string(m.sample_signedness));
    obj.insert("padding", padding_string(m.sample_padding));
    obj.insert("bytes_per_sample", m.bytes_per_sample);
    obj.insert("bits_per_sample", m.bits_per_sample);
    obj.insert("number_of_channels", m.number_of_channels);
    return obj;
  }

 private:
  std::vector<pcmaudio_metadata> forward_index_;
  std::map<pcmaudio_metadata, size_t> reverse_index_;
};

class pcmaudio_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class pcmaudio_categorizer_ final : public pcmaudio_categorizer_base {
 public:
  pcmaudio_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  inode_fragments
  categorize(fs::path const& path, std::span<uint8_t const> data,
             category_mapper const& mapper) const override;

  bool is_single_fragment() const override { return false; }

  folly::dynamic category_metadata(std::string_view category_name,
                                   fragment_category c) const override {
    if (category_name == PCMAUDIO_CATEGORY) {
      DWARFS_CHECK(c.has_subcategory(),
                   "expected PCMAUDIO to have subcategory");
      return meta_.rlock()->lookup(c.subcategory());
    }
    return folly::dynamic();
  }

 private:
  bool check_aiff(inode_fragments& frag, fs::path const& path,
                  std::span<uint8_t const> data,
                  category_mapper const& mapper) const;
  bool
  check_caf(inode_fragments& frag, fs::path const& path,
            std::span<uint8_t const> data, category_mapper const& mapper) const;
  bool
  check_wav(inode_fragments& frag, fs::path const& path,
            std::span<uint8_t const> data, category_mapper const& mapper) const;
  bool check_wav64(inode_fragments& frag, fs::path const& path,
                   std::span<uint8_t const> data,
                   category_mapper const& mapper) const;

  LOG_PROXY_DECL(LoggerPolicy);
  folly::Synchronized<pcmaudio_metadata_store> mutable meta_;
};

std::span<std::string_view const>
pcmaudio_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      METADATA_CATEGORY,
      PCMAUDIO_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_aiff(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  if (std::memcmp(data.data(), "FORM", 4) != 0 ||
      std::memcmp(data.data() + 8, "AIFF", 4) != 0) {
    return false;
  }

  FOLLY_PACK_PUSH

  struct chk_hdr_t {
    char id[4];
    uint32_t size;
  } FOLLY_PACK_ATTR;

  struct comm_chk_t {
    uint16_t num_chan;
    uint32_t num_sample_frames;
    uint16_t sample_size;
    // long double sample_rate;  // we can't pack this :/
  } FOLLY_PACK_ATTR;

  struct ssnd_chk_t {
    uint32_t offset;
    uint32_t block_size;
  } FOLLY_PACK_ATTR;

  FOLLY_PACK_POP

  static_assert(sizeof(chk_hdr_t) == 8);
  static_assert(sizeof(comm_chk_t) == 8);
  static_assert(sizeof(ssnd_chk_t) == 8);

  bool meta_valid{false};
  uint32_t num_sample_frames;
  pcmaudio_metadata meta;
  size_t pos = 12;
  chk_hdr_t chk_hdr;

  while (pos + sizeof(chk_hdr) <= data.size()) {
    std::memcpy(&chk_hdr, data.data() + pos, sizeof(chk_hdr));
    uint32_t chk_size = folly::Endian::big(chk_hdr.size);

    LOG_TRACE << "[AIFF] " << path << ": " << std::string_view(chk_hdr.id, 4)
              << " (len=" << chk_size << ")";

    if (pos + sizeof(chk_hdr) + chk_size > data.size()) {
      LOG_WARN << "[AIFF] " << path << ": unexpected end of file";
      // corrupt AIFF? -> skip
      return false;
    }

    if (std::memcmp(chk_hdr.id, "COMM", 4) == 0) {
      if (chk_size != 18) {
        LOG_WARN << "[AIFF] " << path
                 << ": unexpected size for COMM chunk: " << chk_size
                 << " (expected 18)";
        // corrupt AIFF? -> skip
        return false;
      }

      if (meta_valid) {
        LOG_WARN << "[AIFF] " << path << ": unexpected second COMM chunk";
        // corrupt AIFF? -> skip
        return false;
      }

      comm_chk_t comm;
      std::memcpy(&comm, data.data() + pos + sizeof(chk_hdr), sizeof(comm));

      meta.sample_endianness = endianness::BIG;
      meta.sample_signedness = signedness::SIGNED;
      meta.sample_padding = padding::LSB;
      meta.bits_per_sample = folly::Endian::big(comm.sample_size);
      meta.bytes_per_sample = (meta.bits_per_sample + 7) / 8;
      meta.number_of_channels = folly::Endian::big(comm.num_chan);
      num_sample_frames = folly::Endian::big(comm.num_sample_frames);

      if (meta.bits_per_sample < 8 || meta.bits_per_sample > 32) {
        LOG_WARN << "[AIFF] " << path
                 << ": unsupported bits per sample: " << meta.bits_per_sample;
        return false;
      }

      if (meta.number_of_channels == 0) {
        LOG_WARN << "[AIFF] " << path << ": file has no audio channels";
        return false;
      }

      meta_valid = true;

      LOG_TRACE << "[AIFF] " << path << ": meta=" << meta;
    } else if (std::memcmp(chk_hdr.id, "SSND", 4) == 0) {
      if (!meta_valid) {
        LOG_WARN << "[AIFF] " << path << ": got SSND chunk without COMM chunk";
        // corrupt AIFF? -> skip
        return false;
      }

      ssnd_chk_t ssnd;
      std::memcpy(&ssnd, data.data() + pos + sizeof(chk_hdr), sizeof(ssnd));
      ssnd.offset = folly::Endian::big(ssnd.offset);
      ssnd.block_size = folly::Endian::big(ssnd.block_size);

      size_t pcm_start = pos + sizeof(chk_hdr) + sizeof(ssnd) + ssnd.offset;
      size_t pcm_length =
          num_sample_frames * (meta.number_of_channels * meta.bytes_per_sample);

      if (sizeof(ssnd) + ssnd.offset + pcm_length > chk_size) {
        LOG_WARN << "[AIFF] " << path
                 << ": SSND invalid chunk size (offset=" << ssnd.offset
                 << ", pcm_len=" << pcm_length << ", chk_size" << chk_size
                 << ")";
        // corrupt AIFF? -> skip
        return false;
      }

      fragment_category::value_type subcategory = meta_.wlock()->add(meta);

      frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                        pcm_start);
      frag.emplace_back(
          fragment_category(mapper(PCMAUDIO_CATEGORY), subcategory),
          pcm_length);

      if (pcm_start + pcm_length < data.size()) {
        frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                          data.size() - (pcm_start + pcm_length));
      }

      return true;
    }

    pos += sizeof(chk_hdr) + chk_size;
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_caf(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  if (std::memcmp(data.data(), "caff", 4) != 0) {
    return false;
  }

  FOLLY_PACK_PUSH

  struct caff_hdr_t {
    uint16_t version;
    uint16_t flags;
  } FOLLY_PACK_ATTR;

  struct chk_hdr_t {
    char id[4];
    uint64_t size;
  } FOLLY_PACK_ATTR;

  struct format_chk_t {
    double sample_rate;
    char format_id[4];
    uint32_t format_flags;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint32_t channels_per_frame;
    uint32_t bits_per_channel;
  } FOLLY_PACK_ATTR;

  struct data_chk_t {
    uint32_t edit_count;
  } FOLLY_PACK_ATTR;

  FOLLY_PACK_POP

  static_assert(sizeof(caff_hdr_t) == 4);
  static_assert(sizeof(chk_hdr_t) == 12);
  static_assert(sizeof(format_chk_t) == 32);
  static_assert(sizeof(data_chk_t) == 4);

  static constexpr uint32_t const kCAFLinearPCMFormatFlagIsFloat{1L << 0};
  static constexpr uint32_t const kCAFLinearPCMFormatFlagIsLittleEndian{1L
                                                                        << 1};

  caff_hdr_t caff_hdr;
  std::memcpy(&caff_hdr, data.data() + 4, sizeof(caff_hdr));
  caff_hdr.version = folly::Endian::big(caff_hdr.version);
  caff_hdr.flags = folly::Endian::big(caff_hdr.flags);

  if (caff_hdr.version != 1 || caff_hdr.flags != 0) {
    LOG_WARN << "[CAF] " << path
             << ": unsupported file version/flags: " << caff_hdr.version << "/"
             << caff_hdr.flags;
    return false;
  }

  bool meta_valid{false};
  pcmaudio_metadata meta;
  size_t pos = 8;
  chk_hdr_t chk_hdr;

  while (pos + sizeof(chk_hdr) <= data.size()) {
    std::memcpy(&chk_hdr, data.data() + pos, sizeof(chk_hdr));
    uint64_t chk_size = folly::Endian::big(chk_hdr.size);

    LOG_TRACE << "[CAF] " << path << ": " << std::string_view(chk_hdr.id, 4)
              << " (len=" << chk_size << ")";

    if (chk_size == std::numeric_limits<uint64_t>::max() &&
        std::memcmp(chk_hdr.id, "data", 4) == 0) {
      chk_size = data.size() - (pos + sizeof(chk_hdr));
    }

    if (pos + sizeof(chk_hdr) + chk_size > data.size()) {
      LOG_WARN << "[CAF] " << path << ": unexpected end of file";
      // corrupt CAF? -> skip
      return false;
    }

    if (std::memcmp(chk_hdr.id, "desc", 4) == 0) {
      if (chk_size != sizeof(format_chk_t)) {
        LOG_WARN << "[CAF] " << path
                 << ": unexpected size for desc chunk: " << chk_size
                 << " (expected " << sizeof(format_chk_t) << ")";
        // corrupt CAF? -> skip
        return false;
      }

      if (meta_valid) {
        LOG_WARN << "[CAF] " << path << ": unexpected second desc chunk";
        // corrupt CAF? -> skip
        return false;
      }

      format_chk_t fmt;
      std::memcpy(&fmt, data.data() + pos + sizeof(chk_hdr), sizeof(fmt));

      if (std::memcmp(fmt.format_id, "lpcm", 4) != 0) {
        // TODO: alaw, ulaw?
        LOG_DEBUG << "[CAF] " << path << ": found compressed format";
        return false;
      }

      fmt.format_flags = folly::Endian::big(fmt.format_flags);

      if (fmt.format_flags & kCAFLinearPCMFormatFlagIsFloat) {
        LOG_DEBUG << "[CAF] " << path << ": floating point is unsupported";
        return false;
      }

      fmt.frames_per_packet = folly::Endian::big(fmt.frames_per_packet);

      if (fmt.frames_per_packet != 1) {
        LOG_WARN << "[CAF] " << path
                 << ": unsupported frames/packet: " << fmt.frames_per_packet;
        return false;
      }

      fmt.bytes_per_packet = folly::Endian::big(fmt.bytes_per_packet);

      meta.sample_endianness =
          (fmt.format_flags & kCAFLinearPCMFormatFlagIsLittleEndian)
              ? endianness::LITTLE
              : endianness::BIG;
      meta.sample_signedness = signedness::SIGNED;
      meta.sample_padding = padding::LSB;
      meta.bits_per_sample = folly::Endian::big(fmt.bits_per_channel);
      meta.number_of_channels = folly::Endian::big(fmt.channels_per_frame);

      if (meta.bits_per_sample < 8 || meta.bits_per_sample > 32) {
        LOG_WARN << "[CAF] " << path
                 << ": unsupported bits per sample: " << meta.bits_per_sample;
        return false;
      }

      if (meta.number_of_channels == 0) {
        LOG_WARN << "[CAF] " << path << ": file has no audio channels";
        return false;
      }

      if (fmt.bytes_per_packet == 0) {
        LOG_WARN << "[CAF] " << path << ": bytes per packet is zero";
        return false;
      }

      if (fmt.bytes_per_packet > 4 * meta.number_of_channels) {
        LOG_WARN << "[CAF] " << path
                 << ": bytes per packet out of range: " << fmt.bytes_per_packet;
        return false;
      }

      if (fmt.bytes_per_packet % meta.number_of_channels != 0) {
        LOG_WARN << "[CAF] " << path
                 << ": unsupported packet size: " << fmt.bytes_per_packet
                 << " (" << meta.number_of_channels << " channels)";
        return false;
      }

      meta.bytes_per_sample = fmt.bytes_per_packet / meta.number_of_channels;

      assert(meta.bytes_per_sample > 0);

      meta_valid = true;

      LOG_TRACE << "[CAF] " << path << ": meta=" << meta;
    } else if (std::memcmp(chk_hdr.id, "data", 4) == 0) {
      if (!meta_valid) {
        LOG_WARN << "[CAF] " << path << ": got data chunk without desc chunk";
        // corrupt CAF? -> skip
        return false;
      }

      size_t pcm_start = pos + sizeof(chk_hdr) + sizeof(data_chk_t);
      size_t pcm_length = chk_size - sizeof(data_chk_t);

      if (pcm_length % (meta.number_of_channels * meta.bytes_per_sample)) {
        LOG_WARN << "[CAF] " << path
                 << ": data chunk size mismatch (pcm_len=" << pcm_length
                 << ", #chan=" << meta.number_of_channels
                 << ", bytes_per_sample=" << meta.bytes_per_sample << ")";
        // corrupt CAF? -> skip
        return false;
      }

      fragment_category::value_type subcategory = meta_.wlock()->add(meta);

      frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                        pcm_start);
      frag.emplace_back(
          fragment_category(mapper(PCMAUDIO_CATEGORY), subcategory),
          pcm_length);

      if (pcm_start + pcm_length < data.size()) {
        frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                          data.size() - (pcm_start + pcm_length));
      }

      return true;
    }

    pos += sizeof(chk_hdr) + chk_size;
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  if (std::memcmp(data.data(), "RIFF", 4) != 0) {
    return false;
  }
  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav64(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  if (std::memcmp(data.data(), "riff", 4) != 0) {
    return false;
  }
  return false;
}

template <typename LoggerPolicy>
inode_fragments pcmaudio_categorizer_<LoggerPolicy>::categorize(
    fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  inode_fragments fragments;

  if (data.size() >= MIN_PCMAUDIO_SIZE) {
    for (auto f : {
             // clang-format off
             &pcmaudio_categorizer_::check_aiff,
             &pcmaudio_categorizer_::check_caf,
             &pcmaudio_categorizer_::check_wav,
             &pcmaudio_categorizer_::check_wav64,
             // clang-format on
         }) {
      if ((this->*f)(fragments, path, data, mapper)) {
        break;
      }

      // clean up
      fragments.clear();
    }
  }

  return fragments;
}

class pcmaudio_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "pcmaudio"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/) const override {
    return make_unique_logging_object<categorizer, pcmaudio_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(pcmaudio_categorizer_factory)

} // namespace dwarfs
