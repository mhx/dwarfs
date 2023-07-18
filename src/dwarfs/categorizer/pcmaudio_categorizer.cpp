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

  bool check() const {
    // make sure we're supporting a reasonable subset

    if (number_of_channels == 0) {
      return false;
    }

    if (!(bits_per_sample == 8 || bits_per_sample == 16 ||
          bits_per_sample == 20 || bits_per_sample == 24 ||
          bits_per_sample == 32)) {
      return false;
    }

    if (bits_per_sample == 8 && bytes_per_sample != 1) {
      return false;
    }

    if (bits_per_sample == 16 && bytes_per_sample != 2) {
      return false;
    }

    if ((bits_per_sample == 20 || bits_per_sample == 24) &&
        !(bytes_per_sample == 3 || bytes_per_sample == 4)) {
      return false;
    }

    if (bits_per_sample == 32 && bytes_per_sample != 4) {
      return false;
    }

    return true;
  }
};

template <endianness>
struct endian;

template <>
struct endian<endianness::BIG> {
  template <typename T>
  static T convert(T x) {
    return folly::Endian::big(x);
  }
};

template <>
struct endian<endianness::LITTLE> {
  template <typename T>
  static T convert(T x) {
    return folly::Endian::little(x);
  }
};

struct WavPolicy {
  using SizeType = uint32_t;
  static constexpr bool const size_includes_header{false};
  static constexpr size_t const id_size{4};
  static constexpr size_t const file_header_size{12};
  static constexpr size_t const chunk_header_size{8};
  static constexpr std::string_view const format_name{"WAV"};
  static constexpr std::string_view const file_header_id{"RIFF"};
  static constexpr std::string_view const wave_id{"WAVE"};
  static constexpr std::string_view const fmt_id{"fmt "};
  static constexpr std::string_view const data_id{"data"};
};

struct Wav64Policy {
  using SizeType = uint64_t;
  static constexpr bool const size_includes_header{true};
  static constexpr size_t const id_size{16};
  static constexpr size_t const file_header_size{40};
  static constexpr size_t const chunk_header_size{24};
  static constexpr std::string_view const format_name{"WAV64"};
  static constexpr std::string_view const file_header_id{
      "riff\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00", id_size};
  static constexpr std::string_view const wave_id{
      "wave\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a", id_size};
  static constexpr std::string_view const fmt_id{
      "fmt \xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a", id_size};
  static constexpr std::string_view const data_id{
      "data\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a", id_size};
};

template <typename LoggerPolicy, typename ChunkHeaderType,
          endianness Endianness, bool IsCaf = false,
          bool SizeIncludesHeader = false>
class iff_parser final {
 public:
  struct chunk {
    ChunkHeaderType header;
    size_t pos;

    bool is(std::string_view id) const {
      assert(sizeof(header.id) == id.size());
      return std::memcmp(header.id, id.data(), sizeof(header.id)) == 0;
    }

    std::string_view id() const {
      return std::string_view(header.id, sizeof(header.id));
    }

    std::string_view fourcc() const {
      static_assert(sizeof(header.id) >= 4);
      return std::string_view(header.id, 4);
    }

    size_t size() const { return header.size; }
  };

  iff_parser(logger& lgr, std::string_view name, fs::path const& path,
             std::span<uint8_t const> data, size_t pos)
      : LOG_PROXY_INIT(lgr)
      , data_{data}
      , name_{name}
      , path_{path}
      , pos_{pos} {}

  std::optional<chunk> next_chunk() {
    std::optional<chunk> c;

    if (pos_ + sizeof(ChunkHeaderType) <= data_.size()) {
      c.emplace();

      DWARFS_CHECK(read(c->header, pos_), "iff_parser::read failed");
      c->header.size = endian<Endianness>::convert(c->header.size);
      c->pos = pos_;

      if constexpr (IsCaf) {
        if (c->header.size ==
                std::numeric_limits<decltype(c->header.size)>::max() &&
            c->is("data")) {
          c->header.size = data_.size() - (pos_ + sizeof(ChunkHeaderType));
        }
      }

      if constexpr (!SizeIncludesHeader) {
        pos_ += sizeof(ChunkHeaderType);
      }

      pos_ += c->header.size;

      if (pos_ > data_.size()) {
        LOG_WARN << "[" << name_ << "] " << path_
                 << ": unexpected end of file (pos=" << pos_
                 << ", hdr.size=" << c->header.size << ", end=" << data_.size()
                 << ")";
        c.reset();
      }

      LOG_TRACE << "[" << name_ << "] " << path_ << ": `" << c->fourcc()
                << "` (len=" << c->size() << ")";
    }

    return c;
  }

  template <typename T>
  bool read(T& storage, chunk const& c) const {
    return read(storage, c, sizeof(storage));
  }

  template <typename T>
  bool read(T& storage, chunk const& c, size_t len) const {
    assert(len <= c.size());
    return read(storage, c.pos + sizeof(ChunkHeaderType), len);
  }

  template <typename T>
  bool read_file_header(T& storage) const {
    return read(storage, 0, sizeof(storage));
  }

  bool expected_size(chunk c, size_t expected_size) const {
    if (c.size() == expected_size) {
      return true;
    }

    LOG_WARN << "[" << name_ << "] " << path_ << ": unexpected size for `"
             << c.fourcc() << "` chunk: " << c.size() << " (expected "
             << expected_size << ")";

    return false;
  }

 private:
  template <typename T>
  bool read(T& storage, size_t pos) const {
    return read(storage, pos, sizeof(storage));
  }

  template <typename T>
  bool read(T& storage, size_t pos, size_t len) const {
    assert(len <= sizeof(storage));

    if (pos + len <= data_.size()) {
      std::memcpy(&storage, data_.data() + pos, len);
      return true;
    }

    LOG_WARN << "[" << name_ << "] " << path_ << ": unexpected end of file";

    return false;
  }

  LOG_PROXY_DECL(LoggerPolicy);
  std::span<uint8_t const> data_;
  std::string_view name_;
  fs::path const& path_;
  size_t pos_;
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

  template <typename FormatPolicy>
  bool check_wav_like(inode_fragments& frag, fs::path const& path,
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

  struct file_hdr_t {
    char id[4];
    uint32_t size;
    char form[4];
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
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

  static_assert(sizeof(chunk_hdr_t) == 8);
  static_assert(sizeof(comm_chk_t) == 8);
  static_assert(sizeof(ssnd_chk_t) == 8);

  iff_parser<LoggerPolicy, chunk_hdr_t, endianness::BIG> parser(
      LOG_GET_LOGGER, "AIFF", path, data, sizeof(file_hdr_t));

  bool meta_valid{false};
  uint32_t num_sample_frames;
  pcmaudio_metadata meta;

  while (auto chunk = parser.next_chunk()) {
    if (chunk->is("COMM")) {
      if (parser.expected_size(*chunk, 18)) {
        return false;
      }

      if (meta_valid) {
        LOG_WARN << "[AIFF] " << path << ": unexpected second `COMM` chunk";
        return false;
      }

      comm_chk_t comm;
      if (!parser.read(comm, *chunk)) {
        return false;
      }

      meta.sample_endianness = endianness::BIG;
      meta.sample_signedness = signedness::SIGNED;
      meta.sample_padding = padding::LSB;
      meta.bits_per_sample = folly::Endian::big(comm.sample_size);
      meta.bytes_per_sample = (meta.bits_per_sample + 7) / 8;
      meta.number_of_channels = folly::Endian::big(comm.num_chan);
      num_sample_frames = folly::Endian::big(comm.num_sample_frames);

      if (!meta.check()) {
        LOG_WARN << "[AIFF] " << path << ": metadata check failed: " << meta;
        return false;
      }

      meta_valid = true;

      LOG_TRACE << "[AIFF] " << path << ": meta=" << meta;
    } else if (chunk->is("SSND")) {
      if (!meta_valid) {
        LOG_WARN << "[AIFF] " << path
                 << ": got `SSND` chunk without `COMM` chunk";
        return false;
      }

      ssnd_chk_t ssnd;
      if (!parser.read(ssnd, *chunk)) {
        return false;
      }

      ssnd.offset = folly::Endian::big(ssnd.offset);
      ssnd.block_size = folly::Endian::big(ssnd.block_size);

      size_t pcm_start =
          chunk->pos + sizeof(chunk_hdr_t) + sizeof(ssnd) + ssnd.offset;
      size_t pcm_length =
          num_sample_frames * (meta.number_of_channels * meta.bytes_per_sample);

      if (sizeof(ssnd) + ssnd.offset + pcm_length > chunk->size()) {
        LOG_WARN << "[AIFF] " << path
                 << ": SSND invalid chunk size (offset=" << ssnd.offset
                 << ", pcm_len=" << pcm_length << ", chk_size" << chunk->size()
                 << ")";
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
    char id[4];
    uint16_t version;
    uint16_t flags;
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
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

  static_assert(sizeof(caff_hdr_t) == 8);
  static_assert(sizeof(chunk_hdr_t) == 12);
  static_assert(sizeof(format_chk_t) == 32);
  static_assert(sizeof(data_chk_t) == 4);

  static constexpr uint32_t const kCAFLinearPCMFormatFlagIsFloat{1L << 0};
  static constexpr uint32_t const kCAFLinearPCMFormatFlagIsLittleEndian{1L
                                                                        << 1};

  iff_parser<LoggerPolicy, chunk_hdr_t, endianness::BIG, true> parser(
      LOG_GET_LOGGER, "CAF", path, data, sizeof(caff_hdr_t));

  caff_hdr_t caff_hdr;
  if (!parser.read_file_header(caff_hdr)) {
    return false;
  }

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

  while (auto chunk = parser.next_chunk()) {
    if (chunk->is("desc")) {
      if (!parser.expected_size(*chunk, sizeof(format_chk_t))) {
        return false;
      }

      if (meta_valid) {
        LOG_WARN << "[CAF] " << path << ": unexpected second `desc` chunk";
        return false;
      }

      format_chk_t fmt;
      if (!parser.read(fmt, *chunk)) {
        return false;
      }

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

      if (!meta.check()) {
        LOG_WARN << "[CAF] " << path << ": metadata check failed: " << meta;
        return false;
      }

      meta_valid = true;

      LOG_TRACE << "[CAF] " << path << ": meta=" << meta;
    } else if (chunk->is("data")) {
      if (!meta_valid) {
        LOG_WARN << "[CAF] " << path
                 << ": got `data` chunk without `desc` chunk";
        return false;
      }

      size_t pcm_start = chunk->pos + sizeof(chunk_hdr_t) + sizeof(data_chk_t);
      size_t pcm_length = chunk->size() - sizeof(data_chk_t);

      if (pcm_length % (meta.number_of_channels * meta.bytes_per_sample)) {
        LOG_WARN << "[CAF] " << path
                 << ": `data` chunk size mismatch (pcm_len=" << pcm_length
                 << ", #chan=" << meta.number_of_channels
                 << ", bytes_per_sample=" << meta.bytes_per_sample << ")";
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
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  return check_wav_like<WavPolicy>(frag, path, data, mapper);
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav64(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  return check_wav_like<Wav64Policy>(frag, path, data, mapper);
}

template <typename LoggerPolicy>
template <typename FormatPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav_like(
    inode_fragments& frag, fs::path const& path, std::span<uint8_t const> data,
    category_mapper const& mapper) const {
  if (std::memcmp(data.data(), FormatPolicy::file_header_id.data(),
                  FormatPolicy::id_size) != 0) {
    return false;
  }

  FOLLY_PACK_PUSH

  struct file_hdr_t {
    char id[FormatPolicy::id_size];
    typename FormatPolicy::SizeType size;
    char form[FormatPolicy::id_size];
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
    char id[FormatPolicy::id_size];
    typename FormatPolicy::SizeType size;
  } FOLLY_PACK_ATTR;

  struct fmt_chunk_t {
    uint16_t format_code;
    uint16_t num_channels;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t ext_size;
    uint16_t valid_bits_per_sample;
    uint32_t channel_mask;
    uint16_t sub_format_code;
    uint8_t guid_remainder[14];
  } FOLLY_PACK_ATTR;

  FOLLY_PACK_POP

  static_assert(sizeof(file_hdr_t) == FormatPolicy::file_header_size);
  static_assert(sizeof(chunk_hdr_t) == FormatPolicy::chunk_header_size);

  static constexpr uint16_t const WAVE_FORMAT_PCM{0x0001};
  static constexpr uint16_t const WAVE_FORMAT_EXTENSIBLE{0xFFFE};

  iff_parser<LoggerPolicy, chunk_hdr_t, endianness::LITTLE, false,
             FormatPolicy::size_includes_header>
      parser(LOG_GET_LOGGER, FormatPolicy::format_name, path, data,
             sizeof(file_hdr_t));

  file_hdr_t file_header;
  if (!parser.read_file_header(file_header)) {
    return false;
  }

  if (std::memcmp(file_header.form, FormatPolicy::wave_id.data(),
                  FormatPolicy::id_size) != 0) {
    return false;
  }

  bool meta_valid{false};
  pcmaudio_metadata meta;

  while (auto chunk = parser.next_chunk()) {
    if (chunk->is(FormatPolicy::fmt_id)) {
      if (chunk->size() != 16 && chunk->size() != 18 && chunk->size() != 40) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": unexpected size for `" << chunk->fourcc()
                 << "` chunk: " << chunk->size() << " (expected 16, 18, 40)";
        return false;
      }

      if (meta_valid) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": unexpected second `" << chunk->fourcc() << "` chunk";
        return false;
      }

      fmt_chunk_t fmt;
      if (!parser.read(fmt, *chunk, chunk->size())) {
        return false;
      }

      fmt.format_code = folly::Endian::little(fmt.format_code);
      fmt.num_channels = folly::Endian::little(fmt.num_channels);
      fmt.samples_per_sec = folly::Endian::little(fmt.samples_per_sec);
      fmt.avg_bytes_per_sec = folly::Endian::little(fmt.avg_bytes_per_sec);
      fmt.block_align = folly::Endian::little(fmt.block_align);
      fmt.bits_per_sample = folly::Endian::little(fmt.bits_per_sample);
      if (chunk->size() == 40) {
        fmt.valid_bits_per_sample =
            folly::Endian::little(fmt.valid_bits_per_sample);
        fmt.sub_format_code = folly::Endian::little(fmt.sub_format_code);
      } else {
        fmt.sub_format_code = 0;
      }

      if (!(fmt.format_code == WAVE_FORMAT_PCM ||
            (fmt.format_code == WAVE_FORMAT_EXTENSIBLE && chunk->size() == 40 &&
             fmt.sub_format_code == WAVE_FORMAT_PCM))) {
        LOG_TRACE << "[" << FormatPolicy::format_name << "] " << path
                  << ": unsupported format: " << fmt.format_code << "/"
                  << fmt.sub_format_code;
        return false;
      }

      meta.sample_endianness = endianness::LITTLE;
      meta.sample_signedness =
          fmt.bits_per_sample > 8 ? signedness::SIGNED : signedness::UNSIGNED;
      meta.sample_padding = padding::LSB;
      meta.bits_per_sample = fmt.bits_per_sample;
      meta.bytes_per_sample = (meta.bits_per_sample + 7) / 8;
      meta.number_of_channels = fmt.num_channels;

      if (!meta.check()) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": metadata check failed: " << meta;
        return false;
      }

      meta_valid = true;

      LOG_TRACE << "[" << FormatPolicy::format_name << "] " << path
                << ": meta=" << meta;
    } else if (chunk->is(FormatPolicy::data_id)) {
      if (!meta_valid) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": got `data` chunk without `fmt ` chunk";
        return false;
      }

      size_t pcm_start = chunk->pos + sizeof(chunk_hdr_t);
      size_t pcm_length = chunk->size();

      if (pcm_length % (meta.number_of_channels * meta.bytes_per_sample)) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": `data` chunk size mismatch (pcm_len=" << pcm_length
                 << ", #chan=" << meta.number_of_channels
                 << ", bytes_per_sample=" << meta.bytes_per_sample << ")";
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
