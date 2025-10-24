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
#include <cassert>
#include <concepts>
#include <cstring>
#include <map>
#include <shared_mutex>
#include <stack>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <folly/Synchronized.h>
#include <folly/lang/Bits.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/map_util.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/compression_metadata_requirements.h>

namespace dwarfs::writer {

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

constexpr std::string_view const METADATA_CATEGORY{"pcmaudio/metadata"};
constexpr std::string_view const WAVEFORM_CATEGORY{"pcmaudio/waveform"};

constexpr file_size_t const MIN_PCMAUDIO_SIZE{32};

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

std::ostream& operator<<(std::ostream& os, endianness e) {
  switch (e) {
  case endianness::BIG:
    os << "big";
    break;
  case endianness::LITTLE:
    os << "little";
    break;
  default:
    DWARFS_PANIC("internal error: unhandled endianness value");
  }
  return os;
}

std::optional<endianness> parse_endianness(std::string_view e) {
  static std::unordered_map<std::string_view, endianness> const lookup{
      {"big", endianness::BIG},
      {"little", endianness::LITTLE},
  };
  return get_optional(lookup, e);
}

std::optional<endianness> parse_endianness_dyn(nlohmann::json const& e) {
  return parse_endianness(e.get<std::string>());
}

std::ostream& operator<<(std::ostream& os, signedness e) {
  switch (e) {
  case signedness::SIGNED:
    os << "signed";
    break;
  case signedness::UNSIGNED:
    os << "unsigned";
    break;
  default:
    DWARFS_PANIC("internal error: unhandled signedness value");
  }
  return os;
}

std::optional<signedness> parse_signedness(std::string_view s) {
  static std::unordered_map<std::string_view, signedness> const lookup{
      {"signed", signedness::SIGNED},
      {"unsigned", signedness::UNSIGNED},
  };
  return get_optional(lookup, s);
}

std::optional<signedness> parse_signedness_dyn(nlohmann::json const& s) {
  return parse_signedness(s.get<std::string>());
}

std::ostream& operator<<(std::ostream& os, padding e) {
  switch (e) {
  case padding::LSB:
    os << "lsb";
    break;
  case padding::MSB:
    os << "msb";
    break;
  default:
    DWARFS_PANIC("internal error: unhandled padding value");
  }
  return os;
}

std::optional<padding> parse_padding(std::string_view p) {
  static std::unordered_map<std::string_view, padding> const lookup{
      {"lsb", padding::LSB},
      {"msb", padding::MSB},
  };
  return get_optional(lookup, p);
}

std::optional<padding> parse_padding_dyn(nlohmann::json const& p) {
  return parse_padding(p.get<std::string>());
}

} // namespace
} // namespace dwarfs::writer

template <>
struct fmt::formatter<dwarfs::writer::endianness> : ostream_formatter {};
template <>
struct fmt::formatter<dwarfs::writer::signedness> : ostream_formatter {};
template <>
struct fmt::formatter<dwarfs::writer::padding> : ostream_formatter {};

namespace dwarfs::writer {
namespace {

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

    if (bits_per_sample != 8 && bits_per_sample != 16 &&
        bits_per_sample != 20 && bits_per_sample != 24 &&
        bits_per_sample != 32) {
      return false;
    }

    if (bits_per_sample == 8 && bytes_per_sample != 1) {
      return false;
    }

    if (bits_per_sample == 16 && bytes_per_sample != 2) {
      return false;
    }

    if ((bits_per_sample == 20 || bits_per_sample == 24) &&
        bytes_per_sample != 3 && bytes_per_sample != 4) {
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
  static constexpr size_t const chunk_align{2};
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
  static constexpr size_t const chunk_align{8};
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

struct AiffChunkPolicy {
  static constexpr std::string_view const format_name{"AIFF"};
  static constexpr size_t const alignment{2};
  static constexpr endianness const endian{endianness::BIG};
  static constexpr bool const size_includes_header{false};
  static void preprocess(auto&, file_view const&) {}
};

struct CafChunkPolicy {
  static constexpr std::string_view const format_name{"CAF"};
  static constexpr size_t const alignment{1};
  static constexpr endianness const endian{endianness::BIG};
  static constexpr bool const size_includes_header{false};
  static void preprocess(auto& c, file_view const& mm) {
    if (c.header.size == std::numeric_limits<decltype(c.header.size)>::max() &&
        c.is("data")) {
      c.header.size = mm.size() - (c.pos + sizeof(c.header));
    }
  }
};

template <typename FormatPolicy>
struct WavChunkPolicy {
  static constexpr std::string_view const format_name{
      FormatPolicy::format_name};
  static constexpr size_t const alignment{FormatPolicy::chunk_align};
  static constexpr endianness const endian{endianness::LITTLE};
  static constexpr bool const size_includes_header{
      FormatPolicy::size_includes_header};
  static void preprocess(auto&, file_view const&) {}
};

template <typename LoggerPolicy, typename ChunkPolicy, typename ChunkHeaderType>
class iff_parser final {
 public:
  struct chunk {
    ChunkHeaderType header;
    file_off_t pos;

    static constexpr size_t const kFourCCSize{4};

    constexpr bool is(std::string_view ident) const {
      assert(header.id.size() == ident.size());
      return ident == this->id();
    }

    constexpr std::string_view id() const {
      return {header.id.data(), header.id.size()};
    }

    constexpr std::string_view fourcc() const {
      static_assert(sizeof(header.id) >= kFourCCSize);
      return {header.id.data(), kFourCCSize};
    }

    constexpr file_size_t size() const { return header.size; }
  };

  iff_parser(logger& lgr, fs::path const& path, file_view const& mm,
             file_off_t pos)
      : LOG_PROXY_INIT(lgr)
      , mm_{mm}
      , path_{path}
      , pos_{pos} {}

  template <std::integral T>
  static T align(T x) {
    if constexpr (ChunkPolicy::alignment > 1) {
      auto const offset = x % ChunkPolicy::alignment;
      if (offset != 0) {
        x += ChunkPolicy::alignment - offset;
      }
    }

    return x;
  }

  bool check_size(std::string_view which, file_size_t actual_size,
                  file_size_t expected_size) const {
    if (actual_size != expected_size) {
      if (ChunkPolicy::alignment == 1 || align(actual_size) != expected_size) {
        LOG_VERBOSE << "[" << ChunkPolicy::format_name << "] " << path_
                    << ": unexpected " << which << " size: " << actual_size
                    << " (expected " << expected_size << ")";
        return false;
      }
    }
    return true;
  }

  std::optional<chunk> next_chunk() {
    std::optional<chunk> c;

    pos_ = align(pos_);

    if (std::cmp_less_equal(pos_ + sizeof(ChunkHeaderType), mm_.size())) {
      c.emplace();

      DWARFS_CHECK(read(c->header, pos_), "iff_parser::read failed");
      c->header.size = endian<ChunkPolicy::endian>::convert(c->header.size);
      c->pos = pos_;

      ChunkPolicy::preprocess(*c, mm_);

      if constexpr (ChunkPolicy::size_includes_header) {
        if (c->header.size < sizeof(ChunkHeaderType)) {
          LOG_WARN << "[" << ChunkPolicy::format_name << "] " << path_
                   << ": invalid chunk size: " << c->header.size;
          c.reset();
          return c;
        }
        pos_ += c->header.size;
        c->header.size -= sizeof(ChunkHeaderType);
      } else {
        pos_ += sizeof(ChunkHeaderType);
        pos_ += c->header.size;
      }

      if (pos_ > mm_.size()) {
        LOG_WARN << "[" << ChunkPolicy::format_name << "] " << path_
                 << ": unexpected end of file (pos=" << pos_
                 << ", hdr.size=" << c->header.size << ", end=" << mm_.size()
                 << ")";
        c.reset();
        return c;
      }

      LOG_TRACE << "[" << ChunkPolicy::format_name << "] " << path_ << ": `"
                << c->fourcc() << "` (len=" << c->size() << ")";
    }

    return c;
  }

  template <typename T>
  bool read(T& storage, chunk const& c) const {
    return read(storage, c, sizeof(storage));
  }

  template <typename T>
  bool read(T& storage, chunk const& c, file_size_t len) const {
    assert(len <= c.size());
    return read(storage, c.pos + sizeof(ChunkHeaderType), len);
  }

  template <typename T>
  bool read_file_header(T& storage) const {
    return read(storage, 0, sizeof(storage));
  }

  bool expected_size(chunk c, file_size_t expected_size) const {
    if (c.size() == expected_size) {
      return true;
    }

    LOG_WARN << "[" << ChunkPolicy::format_name << "] " << path_
             << ": unexpected size for `" << c.fourcc()
             << "` chunk: " << c.size() << " (expected " << expected_size
             << ")";

    return false;
  }

 private:
  template <typename T>
  bool read(T& storage, file_off_t pos) const {
    return read(storage, pos, sizeof(storage));
  }

  template <typename T>
  bool read(T& storage, file_off_t pos, file_size_t len) const {
    assert(std::cmp_less_equal(len, sizeof(storage)));

    if (pos + len <= mm_.size()) {
      std::error_code ec;

      mm_.copy_to(storage, pos, len, ec);

      if (ec) {
        LOG_WARN << "[" << ChunkPolicy::format_name << "] " << path_
                 << ": read error at pos " << pos << ": " << ec.message();
        return false;
      }

      return true;
    }

    LOG_WARN << "[" << ChunkPolicy::format_name << "] " << path_
             << ": unexpected end of file";

    return false;
  }

  LOG_PROXY_DECL(LoggerPolicy);
  file_view const& mm_;
  fs::path const& path_;
  file_off_t pos_;
};

std::ostream& operator<<(std::ostream& os, pcmaudio_metadata const& m) {
  os << "[" << m.sample_endianness << ", " << m.sample_signedness << ", "
     << m.sample_padding << ", "
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

  std::string lookup(size_t ix) const {
    auto const& m = DWARFS_NOTHROW(forward_index_.at(ix));
    nlohmann::json obj{
        {"endianness", fmt::format("{}", m.sample_endianness)},
        {"signedness", fmt::format("{}", m.sample_signedness)},
        {"padding", fmt::format("{}", m.sample_padding)},
        {"bytes_per_sample", m.bytes_per_sample},
        {"bits_per_sample", m.bits_per_sample},
        {"number_of_channels", m.number_of_channels},
    };
    return obj.dump();
  }

  bool less(size_t a, size_t b) const {
    auto const& ma = DWARFS_NOTHROW(forward_index_.at(a));
    auto const& mb = DWARFS_NOTHROW(forward_index_.at(b));
    return ma < mb;
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
  explicit pcmaudio_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {
    waveform_req_.add_set("endianness", &pcmaudio_metadata::sample_endianness,
                          parse_endianness_dyn);
    waveform_req_.add_set("signedness", &pcmaudio_metadata::sample_signedness,
                          parse_signedness_dyn);
    waveform_req_.add_set("padding", &pcmaudio_metadata::sample_padding,
                          parse_padding_dyn);
    waveform_req_.add_range<int>("bytes_per_sample",
                                 &pcmaudio_metadata::bytes_per_sample);
    waveform_req_.add_range<int>("bits_per_sample",
                                 &pcmaudio_metadata::bits_per_sample);
    waveform_req_.add_range<int>("number_of_channels",
                                 &pcmaudio_metadata::number_of_channels);
  }

  inode_fragments categorize(file_path_info const& path, file_view const& mm,
                             category_mapper const& mapper) const override;

  std::string category_metadata(std::string_view category_name,
                                fragment_category c) const override {
    if (category_name == WAVEFORM_CATEGORY) {
      DWARFS_CHECK(c.has_subcategory(),
                   "expected PCMAUDIO to have subcategory");
      return meta_.rlock()->lookup(c.subcategory());
    }
    return {};
  }

  void set_metadata_requirements(std::string_view category_name,
                                 std::string_view requirements) override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  bool check_aiff(inode_fragments& frag, fs::path const& path,
                  file_view const& mm, category_mapper const& mapper) const;
  bool check_caf(inode_fragments& frag, fs::path const& path,
                 file_view const& mm, category_mapper const& mapper) const;
  bool check_wav(inode_fragments& frag, fs::path const& path,
                 file_view const& mm, category_mapper const& mapper) const;
  bool check_wav64(inode_fragments& frag, fs::path const& path,
                   file_view const& mm, category_mapper const& mapper) const;

  template <typename FormatPolicy>
  bool check_wav_like(inode_fragments& frag, fs::path const& path,
                      file_view const& mm, category_mapper const& mapper) const;

  bool check_metadata(pcmaudio_metadata const& meta, std::string_view context,
                      fs::path const& path) const;

  template <typename ChunkHdrT, typename ChunkT>
  bool
  handle_pcm_data(std::string_view context, ChunkT const& chunk,
                  fs::path const& path, inode_fragments& frag,
                  category_mapper const& mapper, pcmaudio_metadata const& meta,
                  file_size_t total_size, file_off_t pcm_offset) const;

  void add_fragments(inode_fragments& frag, category_mapper const& mapper,
                     pcmaudio_metadata const& meta, file_size_t total_size,
                     file_off_t pcm_start, file_size_t pcm_length) const;

  LOG_PROXY_DECL(LoggerPolicy);
  folly::Synchronized<pcmaudio_metadata_store, std::shared_mutex> mutable meta_;
  compression_metadata_requirements<pcmaudio_metadata> waveform_req_;
};

std::span<std::string_view const>
pcmaudio_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      METADATA_CATEGORY,
      WAVEFORM_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_aiff(
    inode_fragments& frag, fs::path const& path, file_view const& mm,
    category_mapper const& mapper) const {
  FOLLY_PACK_PUSH

  struct file_hdr_t {
    std::array<char, 4> id;
    uint32_t size;
    std::array<char, 4> form;
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
    std::array<char, 4> id;
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

  using aiff_parser = iff_parser<LoggerPolicy, AiffChunkPolicy, chunk_hdr_t>;

  {
    std::error_code ec;
    auto const d = mm.read_string(0, 12, ec);

    if (ec || !d.starts_with("FORM") || !d.substr(8).starts_with("AIFF")) {
      return false;
    }
  }

  aiff_parser parser(LOG_GET_LOGGER, path, mm, sizeof(file_hdr_t));

  file_hdr_t file_header;
  if (!parser.read_file_header(file_header)) {
    return false;
  }

  file_header.size = folly::Endian::big(file_header.size);

  parser.check_size("file", file_header.size,
                    mm.size() - offsetof(file_hdr_t, form));

  bool meta_valid{false};
  uint32_t num_sample_frames;
  pcmaudio_metadata meta;

  while (auto chunk = parser.next_chunk()) {
    if (chunk->is("COMM")) {
      if (!parser.expected_size(*chunk, 18)) {
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

      meta_valid = check_metadata(meta, "AIFF", path);

      if (!meta_valid) {
        return false;
      }
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

      file_off_t pcm_start =
          chunk->pos + sizeof(chunk_hdr_t) + sizeof(ssnd) + ssnd.offset;
      file_size_t pcm_length =
          num_sample_frames * (meta.number_of_channels * meta.bytes_per_sample);

      if (std::cmp_greater(sizeof(ssnd) + ssnd.offset + pcm_length,
                           chunk->size())) {
        LOG_WARN << "[AIFF] " << path
                 << ": `SSND` invalid chunk size: " << chunk->size()
                 << ", expected >= " << sizeof(ssnd) + ssnd.offset + pcm_length
                 << " (offset=" << ssnd.offset << ", pcm_len=" << pcm_length
                 << ")";
        return false;
      }

      add_fragments(frag, mapper, meta, mm.size(), pcm_start, pcm_length);

      return true;
    }
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_caf(
    inode_fragments& frag, fs::path const& path, file_view const& mm,
    category_mapper const& mapper) const {
  FOLLY_PACK_PUSH

  struct caff_hdr_t {
    std::array<char, 4> id;
    uint16_t version;
    uint16_t flags;
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
    std::array<char, 4> id;
    uint64_t size;
  } FOLLY_PACK_ATTR;

  struct format_chk_t {
    double sample_rate;
    std::array<char, 4> format_id;
    uint32_t format_flags;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint32_t channels_per_frame;
    uint32_t bits_per_channel;

    constexpr std::string_view format_id_sv() const {
      return {format_id.data(), format_id.size()};
    }
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

  using caf_parser = iff_parser<LoggerPolicy, CafChunkPolicy, chunk_hdr_t>;

  {
    std::error_code ec;
    auto const d = mm.read_string(0, 4, ec);

    if (ec || !d.starts_with("caff")) {
      return false;
    }
  }

  caf_parser parser(LOG_GET_LOGGER, path, mm, sizeof(caff_hdr_t));

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

      if (auto fmt_id = fmt.format_id_sv(); fmt_id != "lpcm") {
        // TODO: alaw, ulaw?
        LOG_VERBOSE << "[CAF] " << path << ": unsupported `" << fmt_id
                    << "` format";
        return false;
      }

      fmt.format_flags = folly::Endian::big(fmt.format_flags);

      if (fmt.format_flags & kCAFLinearPCMFormatFlagIsFloat) {
        LOG_VERBOSE << "[CAF] " << path
                    << ": floating point format not supported";
        return false;
      }

      fmt.frames_per_packet = folly::Endian::big(fmt.frames_per_packet);

      if (fmt.frames_per_packet != 1) {
        LOG_WARN << "[CAF] " << path << ": unsupported frames per packet: "
                 << fmt.frames_per_packet;
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
        LOG_WARN << "[CAF] " << path << ": bytes per packet must not be zero";
        return false;
      }

      if (fmt.bytes_per_packet > 4 * meta.number_of_channels) {
        LOG_WARN << "[CAF] " << path
                 << ": bytes per packet out of range: " << fmt.bytes_per_packet
                 << ", expected <= " << 4 * meta.number_of_channels;
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

      meta_valid = check_metadata(meta, "CAF", path);

      if (!meta_valid) {
        return false;
      }
    } else if (chunk->is("data")) {
      if (!meta_valid) {
        LOG_WARN << "[CAF] " << path
                 << ": got `data` chunk without `desc` chunk";
        return false;
      }

      return handle_pcm_data<chunk_hdr_t>("CAF", chunk.value(), path, frag,
                                          mapper, meta, mm.size(),
                                          sizeof(data_chk_t));
    }
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav(
    inode_fragments& frag, fs::path const& path, file_view const& mm,
    category_mapper const& mapper) const {
  return check_wav_like<WavPolicy>(frag, path, mm, mapper);
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav64(
    inode_fragments& frag, fs::path const& path, file_view const& mm,
    category_mapper const& mapper) const {
  return check_wav_like<Wav64Policy>(frag, path, mm, mapper);
}

template <typename LoggerPolicy>
template <typename FormatPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_wav_like(
    inode_fragments& frag, fs::path const& path, file_view const& mm,
    category_mapper const& mapper) const {
  FOLLY_PACK_PUSH

  struct file_hdr_t {
    std::array<char, FormatPolicy::id_size> id;
    typename FormatPolicy::SizeType size;
    std::array<char, FormatPolicy::id_size> form;

    constexpr std::string_view form_sv() const {
      return {form.data(), form.size()};
    }
  } FOLLY_PACK_ATTR;

  struct chunk_hdr_t {
    std::array<char, FormatPolicy::id_size> id;
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
    std::array<uint8_t, 14> guid_remainder;
  } FOLLY_PACK_ATTR;

  FOLLY_PACK_POP

  static_assert(sizeof(file_hdr_t) == FormatPolicy::file_header_size);
  static_assert(sizeof(chunk_hdr_t) == FormatPolicy::chunk_header_size);

  static constexpr uint16_t const DWARFS_WAVE_FORMAT_PCM{0x0001};
  static constexpr uint16_t const DWARFS_WAVE_FORMAT_IEEE_FLOAT{0x0003};
  static constexpr uint16_t const DWARFS_WAVE_FORMAT_EXTENSIBLE{0xFFFE};

  using wav_parser =
      iff_parser<LoggerPolicy, WavChunkPolicy<FormatPolicy>, chunk_hdr_t>;

  {
    std::error_code ec;
    auto const d = mm.read_string(0, FormatPolicy::file_header_id.size(), ec);

    if (ec || !d.starts_with(FormatPolicy::file_header_id)) {
      return false;
    }
  }

  wav_parser parser(LOG_GET_LOGGER, path, mm, sizeof(file_hdr_t));

  file_hdr_t file_header;
  if (!parser.read_file_header(file_header)) {
    return false;
  }

  file_header.size = folly::Endian::little(file_header.size);

  if (file_header.form_sv() != FormatPolicy::wave_id) {
    return false;
  }

  {
    file_size_t const expected_size =
        mm.size() -
        (FormatPolicy::size_includes_header ? 0 : offsetof(file_hdr_t, form));

    parser.check_size("file", file_header.size, expected_size);
  }

  bool meta_valid{false};
  pcmaudio_metadata meta;

  while (auto chunk = parser.next_chunk()) {
    if (chunk->is(FormatPolicy::fmt_id)) {
      if (chunk->size() != 16 && chunk->size() != 18 && chunk->size() != 40) {
        if (chunk->size() == 20 && FormatPolicy::format_name == "WAV") {
          // lots of broken files out there with 20-byte fmt chunks...
          LOG_INFO << "[" << FormatPolicy::format_name << "] " << path
                   << ": accepting legacy 20-byte `fmt ` chunk";
        } else {
          LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                   << ": unexpected size for `" << chunk->fourcc()
                   << "` chunk: " << chunk->size() << " (expected 16, 18, 40)";
          return false;
        }
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

      bool const is_extensible =
          chunk->size() == 40 &&
          fmt.format_code == DWARFS_WAVE_FORMAT_EXTENSIBLE;

      if (is_extensible) {
        fmt.valid_bits_per_sample =
            folly::Endian::little(fmt.valid_bits_per_sample);
        fmt.sub_format_code = folly::Endian::little(fmt.sub_format_code);
      } else {
        fmt.sub_format_code = 0;
      }

      auto is_format = [&fmt, is_extensible](uint16_t code) {
        return code == (is_extensible ? fmt.sub_format_code : fmt.format_code);
      };

      if (!is_format(DWARFS_WAVE_FORMAT_PCM)) {
        if (is_format(DWARFS_WAVE_FORMAT_IEEE_FLOAT)) {
          LOG_INFO << "[" << FormatPolicy::format_name << "] " << path
                   << ": floating point format not supported";
        } else {
          LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                   << ": unsupported format: " << fmt.format_code << "/"
                   << fmt.sub_format_code;
        }
        return false;
      }

      meta.sample_endianness = endianness::LITTLE;
      meta.sample_signedness =
          fmt.bits_per_sample > 8 ? signedness::SIGNED : signedness::UNSIGNED;
      meta.sample_padding = padding::LSB;
      meta.bits_per_sample = fmt.bits_per_sample;
      meta.bytes_per_sample = (meta.bits_per_sample + 7) / 8;
      meta.number_of_channels = fmt.num_channels;

      meta_valid = check_metadata(meta, FormatPolicy::format_name, path);

      if (!meta_valid) {
        return false;
      }
    } else if (chunk->is(FormatPolicy::data_id)) {
      if (!meta_valid) {
        LOG_WARN << "[" << FormatPolicy::format_name << "] " << path
                 << ": got `data` chunk without `fmt ` chunk";
        return false;
      }

      return handle_pcm_data<chunk_hdr_t>(FormatPolicy::format_name,
                                          chunk.value(), path, frag, mapper,
                                          meta, mm.size(), 0);
    }
  }

  return false;
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::check_metadata(
    pcmaudio_metadata const& meta, std::string_view context,
    fs::path const& path) const {
  if (!meta.check()) {
    LOG_WARN << "[" << context << "] " << path
             << ": metadata check failed: " << meta;
    return false;
  }

  try {
    waveform_req_.check(meta);
  } catch (std::exception const& e) {
    LOG_WARN << "[" << context << "] " << path << ": " << e.what();
    return false;
  }

  LOG_TRACE << "[" << context << "] " << path << ": meta=" << meta;

  return true;
}

template <typename LoggerPolicy>
template <typename ChunkHdrT, typename ChunkT>
bool pcmaudio_categorizer_<LoggerPolicy>::handle_pcm_data(
    std::string_view context, ChunkT const& chunk, fs::path const& path,
    inode_fragments& frag, category_mapper const& mapper,
    pcmaudio_metadata const& meta, file_size_t total_size,
    file_off_t pcm_offset) const {
  file_off_t pcm_start = chunk.pos + sizeof(ChunkHdrT) + pcm_offset;
  file_size_t pcm_length = chunk.size() - pcm_offset;

  if (auto pcm_padding =
          pcm_length % (meta.number_of_channels * meta.bytes_per_sample);
      pcm_padding > 0) {
    auto expected_pcm_length = pcm_length - pcm_padding;

    LOG_VERBOSE << "[" << context << "] " << path
                << ": `data` chunk size includes " << pcm_padding
                << " padding byte(s); got " << pcm_length << ", expected "
                << expected_pcm_length << " (#chan=" << meta.number_of_channels
                << ", bytes_per_sample="
                << static_cast<int>(meta.bytes_per_sample) << ")";

    // work around broken Logic Pro files...
    pcm_length -= pcm_padding;
  }

  add_fragments(frag, mapper, meta, total_size, pcm_start, pcm_length);

  return true;
}

template <typename LoggerPolicy>
void pcmaudio_categorizer_<LoggerPolicy>::add_fragments(
    inode_fragments& frag, category_mapper const& mapper,
    pcmaudio_metadata const& meta, file_size_t total_size, file_off_t pcm_start,
    file_size_t pcm_length) const {
  fragment_category::value_type subcategory = meta_.wlock()->add(meta);

  frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)), pcm_start);
  frag.emplace_back(fragment_category(mapper(WAVEFORM_CATEGORY), subcategory),
                    pcm_length);

  if (pcm_start + pcm_length < total_size) {
    frag.emplace_back(fragment_category(mapper(METADATA_CATEGORY)),
                      total_size - (pcm_start + pcm_length));
  }
}

template <typename LoggerPolicy>
inode_fragments pcmaudio_categorizer_<LoggerPolicy>::categorize(
    file_path_info const& path, file_view const& mm,
    category_mapper const& mapper) const {
  inode_fragments fragments;

  if (mm.size() >= MIN_PCMAUDIO_SIZE) {
    for (auto f : {
             // clang-format off
             &pcmaudio_categorizer_::check_aiff,
             &pcmaudio_categorizer_::check_caf,
             &pcmaudio_categorizer_::check_wav,
             &pcmaudio_categorizer_::check_wav64,
             // clang-format on
         }) {
      if ((this->*f)(fragments, path.full_path(), mm, mapper)) {
        break;
      }

      // clean up
      fragments.clear();
    }
  }

  return fragments;
}

template <typename LoggerPolicy>
void pcmaudio_categorizer_<LoggerPolicy>::set_metadata_requirements(
    std::string_view category_name, std::string_view requirements) {
  if (!requirements.empty()) {
    auto req = nlohmann::json::parse(requirements);
    if (category_name == WAVEFORM_CATEGORY) {
      waveform_req_.parse(req);
    } else {
      compression_metadata_requirements().parse(req);
    }
  }
}

template <typename LoggerPolicy>
bool pcmaudio_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category a, fragment_category b) const {
  return meta_.rlock()->less(a.subcategory(), b.subcategory());
}

class pcmaudio_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "pcmaudio"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/,
         std::shared_ptr<file_access const> const& /*fa*/) const override {
    return make_unique_logging_object<categorizer, pcmaudio_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(pcmaudio_categorizer_factory)

} // namespace dwarfs::writer
