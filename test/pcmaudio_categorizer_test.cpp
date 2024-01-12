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

#include <exception>
#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/program_options.hpp>

#include <folly/Portability.h>
#include <folly/String.h>
#include <folly/lang/Bits.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/mmap.h"

#include "test_logger.h"

using namespace dwarfs;
using testing::MatchesRegex;

namespace fs = std::filesystem;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR);

FOLLY_PACK_PUSH

struct aiff_file_hdr_t {
  uint8_t id[4];
  uint32_t size;
  uint8_t form[4];
} FOLLY_PACK_ATTR;

aiff_file_hdr_t as_big_endian(aiff_file_hdr_t const& hdr) {
  aiff_file_hdr_t result{hdr};
  result.size = folly::Endian::big(result.size);
  return result;
}

struct aiff_chunk_hdr_t {
  uint8_t id[4];
  uint32_t size;
} FOLLY_PACK_ATTR;

aiff_chunk_hdr_t as_big_endian(aiff_chunk_hdr_t const& hdr) {
  aiff_chunk_hdr_t result{hdr};
  result.size = folly::Endian::big(result.size);
  return result;
}

struct aiff_comm_chk_t {
  uint16_t num_chan;
  uint32_t num_sample_frames;
  uint16_t sample_size;
  uint8_t sample_rate[10]; // long double, but we can't pack that
} FOLLY_PACK_ATTR;

aiff_comm_chk_t as_big_endian(aiff_comm_chk_t const& chk) {
  aiff_comm_chk_t result{chk};
  result.num_chan = folly::Endian::big(result.num_chan);
  result.num_sample_frames = folly::Endian::big(result.num_sample_frames);
  result.sample_size = folly::Endian::big(result.sample_size);
  return result;
}

struct aiff_ssnd_chk_t {
  uint32_t offset;
  uint32_t block_size;
} FOLLY_PACK_ATTR;

aiff_ssnd_chk_t as_big_endian(aiff_ssnd_chk_t const& chk) {
  aiff_ssnd_chk_t result{chk};
  result.offset = folly::Endian::big(result.offset);
  result.block_size = folly::Endian::big(result.block_size);
  return result;
}

struct caff_file_hdr_t {
  uint8_t id[4];
  uint16_t version;
  uint16_t flags;
} FOLLY_PACK_ATTR;

caff_file_hdr_t as_big_endian(caff_file_hdr_t const& hdr) {
  caff_file_hdr_t result{hdr};
  result.version = folly::Endian::big(result.version);
  result.flags = folly::Endian::big(result.flags);
  return result;
}

struct caff_chunk_hdr_t {
  uint8_t id[4];
  uint64_t size;
} FOLLY_PACK_ATTR;

caff_chunk_hdr_t as_big_endian(caff_chunk_hdr_t const& hdr) {
  caff_chunk_hdr_t result{hdr};
  result.size = folly::Endian::big(result.size);
  return result;
}

struct caff_format_chk_t {
  double sample_rate;
  uint8_t format_id[4];
  uint32_t format_flags;
  uint32_t bytes_per_packet;
  uint32_t frames_per_packet;
  uint32_t channels_per_frame;
  uint32_t bits_per_channel;
} FOLLY_PACK_ATTR;

caff_format_chk_t as_big_endian(caff_format_chk_t const& chk) {
  caff_format_chk_t result{chk};
  result.format_flags = folly::Endian::big(result.format_flags);
  result.bytes_per_packet = folly::Endian::big(result.bytes_per_packet);
  result.frames_per_packet = folly::Endian::big(result.frames_per_packet);
  result.channels_per_frame = folly::Endian::big(result.channels_per_frame);
  result.bits_per_channel = folly::Endian::big(result.bits_per_channel);
  return result;
}

struct caff_data_chk_t {
  uint32_t edit_count;
} FOLLY_PACK_ATTR;

caff_data_chk_t as_big_endian(caff_data_chk_t const& chk) {
  caff_data_chk_t result{chk};
  result.edit_count = folly::Endian::big(result.edit_count);
  return result;
}

struct wav_file_hdr_t {
  uint8_t id[4];
  uint32_t size;
  uint8_t form[4];
} FOLLY_PACK_ATTR;

struct wav_chunk_hdr_t {
  uint8_t id[4];
  uint32_t size;
} FOLLY_PACK_ATTR;

struct wav64_file_hdr_t {
  uint8_t id[16];
  uint64_t size;
  uint8_t form[16];
} FOLLY_PACK_ATTR;

struct wav64_chunk_hdr_t {
  uint8_t id[16];
  uint64_t size;
} FOLLY_PACK_ATTR;

struct wav_fmt_chunk_t {
  uint16_t format_code;
  uint16_t num_channels;
  uint32_t samples_per_sec;
  uint32_t avg_bytes_per_sec;
  uint16_t block_align;
  uint16_t bits_per_sample;
  uint16_t ext_size{0};
  uint16_t valid_bits_per_sample{0};
  uint32_t channel_mask{0};
  uint16_t sub_format_code{0};
  uint8_t guid_remainder[14]{0};
} FOLLY_PACK_ATTR;

FOLLY_PACK_POP

struct pcmfile_builder {
  template <typename T>
  void add(T const& t, size_t size = sizeof(T)) {
    auto const* p = reinterpret_cast<uint8_t const*>(&t);
    data.insert(data.end(), p, p + size);
  }

  void add_bytes(size_t count, uint8_t value) {
    data.insert(data.end(), count, value);
  }

  std::span<uint8_t const> span() const { return data; }

  size_t size() const { return data.size(); }

  std::vector<uint8_t> data;
};

} // namespace

TEST(pcmaudio_categorizer, requirements) {
  test::test_logger logger(logger::INFO);
  boost::program_options::variables_map vm;
  auto& catreg = categorizer_registry::instance();
  auto catmgr = categorizer_manager(logger);

  catmgr.add(catreg.create(logger, "pcmaudio", vm));

  try {
    catmgr.set_metadata_requirements(
        catmgr.category_value("pcmaudio/metadata").value(),
        R"({"endianness": ["set", ["big"]], "bytes_per_sample": ["range", 2, 3]})");
    FAIL() << "expected std::runtime_error";
  } catch (std::runtime_error const& e) {
    EXPECT_STREQ(
        "unsupported metadata requirements: bytes_per_sample, endianness",
        e.what());
  } catch (...) {
    FAIL() << "unexpected exception: "
           << folly::exceptionStr(std::current_exception());
  }

  catmgr.set_metadata_requirements(
      catmgr.category_value("pcmaudio/waveform").value(),
      R"({"endianness": ["set", ["mixed", "big"]], "bytes_per_sample": ["range", 2, 3]})");

  auto wav = test_dir / "pcmaudio" / "test16.wav";
  auto mm = mmap(wav);

  {
    auto job = catmgr.job(wav);
    job.set_total_size(mm.size());

    EXPECT_TRUE(logger.empty());

    job.categorize_random_access(mm.span());
    auto frag = job.result();

    ASSERT_EQ(1, logger.get_log().size());
    auto const& ent = logger.get_log().front();
    EXPECT_EQ(logger::WARN, ent.level);
    EXPECT_THAT(
        ent.output,
        MatchesRegex(
            R"(^\[WAV\] ".*": endianness 'little' does not meet requirements \[big\]$)"));

    EXPECT_TRUE(frag.empty());

    logger.clear();
  }

  catmgr.set_metadata_requirements(
      catmgr.category_value("pcmaudio/waveform").value(),
      R"({"endianness": ["set", ["big", "little"]], "bytes_per_sample": ["range", 1, 4]})");

  {
    auto job = catmgr.job(wav);
    job.set_total_size(mm.size());

    EXPECT_TRUE(logger.empty());

    job.categorize_random_access(mm.span());
    auto frag = job.result();

    EXPECT_TRUE(logger.empty());

    EXPECT_EQ(2, frag.size());

    auto const& first = frag.span()[0];
    auto const& second = frag.span()[1];
    EXPECT_EQ("pcmaudio/metadata",
              catmgr.category_name(first.category().value()));
    EXPECT_EQ(44, first.size());
    EXPECT_EQ("pcmaudio/waveform",
              catmgr.category_name(second.category().value()));
    EXPECT_EQ(14, second.size());
    EXPECT_EQ(mm.size(), first.size() + second.size());
  }
}

class pcmaudio_error_test : public testing::Test {
 public:
  test::test_logger logger{logger::VERBOSE};
  categorizer_manager catmgr{logger};

  auto categorize(pcmfile_builder const& builder) {
    // std::cout << folly::hexDump(builder.data.data(), builder.data.size());
    auto job = catmgr.job(filename());
    job.set_total_size(builder.size());
    job.categorize_random_access(builder.span());
    return job.result();
  }

  void SetUp() override {
    boost::program_options::variables_map vm;
    auto& catreg = categorizer_registry::instance();
    catmgr.add(catreg.create(logger, "pcmaudio", vm));

    catmgr.set_metadata_requirements(
        catmgr.category_value("pcmaudio/waveform").value(),
        R"({"endianness": ["set", ["big", "little"]], "bytes_per_sample": ["range", 1, 4]})");
  }

  virtual std::string_view filename() const = 0;
};

class pcmaudio_error_test_aiff : public pcmaudio_error_test {
 public:
  aiff_file_hdr_t aiff_file_hdr{{'F', 'O', 'R', 'M'}, 62, {'A', 'I', 'F', 'F'}};
  aiff_chunk_hdr_t aiff_comm_chunk_hdr{{'C', 'O', 'M', 'M'}, 18};
  aiff_comm_chk_t aiff_comm_chunk{1, 8, 16, {0}};
  aiff_chunk_hdr_t aiff_ssnd_chunk_hdr{{'S', 'S', 'N', 'D'}, 24};
  aiff_ssnd_chk_t aiff_ssnd_chunk{0, 0};

  pcmfile_builder build_file() {
    pcmfile_builder builder;
    builder.add(as_big_endian(aiff_file_hdr));
    builder.add(as_big_endian(aiff_comm_chunk_hdr));
    builder.add(as_big_endian(aiff_comm_chunk));
    builder.add(as_big_endian(aiff_ssnd_chunk_hdr));
    builder.add(as_big_endian(aiff_ssnd_chunk));
    builder.add_bytes(16, 42);
    return builder;
  }

  std::string_view filename() const override { return "test.aiff"; }
};

class pcmaudio_error_test_caf : public pcmaudio_error_test {
 public:
  caff_file_hdr_t caff_file_hdr{{'c', 'a', 'f', 'f'}, 1, 0};
  caff_chunk_hdr_t caff_format_chunk_hdr{{'d', 'e', 's', 'c'}, 32};
  caff_format_chk_t caff_format_chunk{44100, {'l', 'p', 'c', 'm'}, 0, 2, 1, 1,
                                      16};
  caff_chunk_hdr_t caff_data_chunk_hdr{{'d', 'a', 't', 'a'}, 20};
  caff_data_chk_t caff_data_chunk{0};

  pcmfile_builder build_file() {
    pcmfile_builder builder;
    builder.add(as_big_endian(caff_file_hdr));
    builder.add(as_big_endian(caff_format_chunk_hdr));
    builder.add(as_big_endian(caff_format_chunk));
    builder.add(as_big_endian(caff_data_chunk_hdr));
    builder.add(as_big_endian(caff_data_chunk));
    builder.add_bytes(16, 42);
    return builder;
  }

  std::string_view filename() const override { return "test.caf"; }
};

class pcmaudio_error_test_wav : public pcmaudio_error_test {
 public:
  wav_file_hdr_t wav_file_hdr{{'R', 'I', 'F', 'F'}, 52, {'W', 'A', 'V', 'E'}};
  wav_chunk_hdr_t wav_fmt_chunk_hdr{{'f', 'm', 't', ' '}, 16};
  wav_fmt_chunk_t wav_fmt_chunk{.format_code = 1,
                                .num_channels = 1,
                                .samples_per_sec = 44100,
                                .avg_bytes_per_sec = 44100 * 2,
                                .block_align = 2,
                                .bits_per_sample = 16};
  wav_chunk_hdr_t wav_data_chunk_hdr{{'d', 'a', 't', 'a'}, 16};

  pcmfile_builder build_file() {
    pcmfile_builder builder;
    builder.add(wav_file_hdr);
    builder.add(wav_fmt_chunk_hdr);
    builder.add(wav_fmt_chunk, 16);
    builder.add(wav_data_chunk_hdr);
    builder.add_bytes(16, 42);
    return builder;
  }

  std::string_view filename() const override { return "test.wav"; }
};

class pcmaudio_error_test_wav64 : public pcmaudio_error_test {
 public:
  wav64_file_hdr_t wav_file_hdr{
      {'r', 'i', 'f', 'f', 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04,
       0xc1, 0x00, 0x00},
      120,
      {'w', 'a', 'v', 'e', 0xf3, 0xac, 0xd3, 0x11, 0x8c, 0xd1, 0x00, 0xc0, 0x4f,
       0x8e, 0xdb, 0x8a}};
  wav64_chunk_hdr_t wav_fmt_chunk_hdr{{'f', 'm', 't', ' ', 0xf3, 0xac, 0xd3,
                                       0x11, 0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e,
                                       0xdb, 0x8a},
                                      40};
  wav_fmt_chunk_t wav_fmt_chunk{.format_code = 1,
                                .num_channels = 1,
                                .samples_per_sec = 44100,
                                .avg_bytes_per_sec = 44100 * 2,
                                .block_align = 2,
                                .bits_per_sample = 16};
  wav64_chunk_hdr_t wav_data_chunk_hdr{{'d', 'a', 't', 'a', 0xf3, 0xac, 0xd3,
                                        0x11, 0x8c, 0xd1, 0x00, 0xc0, 0x4f,
                                        0x8e, 0xdb, 0x8a},
                                       40};

  pcmfile_builder build_file() {
    pcmfile_builder builder;
    builder.add(wav_file_hdr);
    builder.add(wav_fmt_chunk_hdr);
    builder.add(wav_fmt_chunk, 16);
    builder.add(wav_data_chunk_hdr);
    builder.add_bytes(16, 42);
    return builder;
  }

  std::string_view filename() const override { return "test.w64"; }
};

TEST_F(pcmaudio_error_test_wav, no_error) {
  auto builder = build_file();
  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(44, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_wav, missing_fmt_chunk) {
  wav_file_hdr.size -= 24;

  pcmfile_builder builder;
  builder.add(wav_file_hdr);
  builder.add(wav_data_chunk_hdr);
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[WAV] \"test.wav\": got `data` chunk without `fmt ` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, unknown_format_code) {
  wav_file_hdr.form[0] = 'F';

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  // not a WAVE file, so we don't expect any warnings
  EXPECT_TRUE(log.empty());
  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, unexpected_fmt_chunk_size) {
  wav_file_hdr.size += 4;
  wav_fmt_chunk_hdr.size += 4;

  pcmfile_builder builder;
  builder.add(wav_file_hdr);
  builder.add(wav_fmt_chunk_hdr);
  builder.add(wav_fmt_chunk, 20);
  builder.add(wav_data_chunk_hdr);
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[WAV] \"test.wav\": unexpected size for `fmt "
                                 "` chunk: 20 (expected 16, 18, 40)"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, unexpected_second_fmt_chunk) {
  wav_file_hdr.size += 24;

  pcmfile_builder builder;
  builder.add(wav_file_hdr);
  builder.add(wav_fmt_chunk_hdr);
  builder.add(wav_fmt_chunk, 16);
  builder.add(wav_fmt_chunk_hdr);
  builder.add(wav_fmt_chunk, 16);
  builder.add(wav_data_chunk_hdr);
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[WAV] \"test.wav\": unexpected second `fmt ` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, unsupported_format_code) {
  wav_fmt_chunk.format_code = 2;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[WAV] \"test.wav\": unsupported format: 2/0"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, metadata_check_failed) {
  wav_fmt_chunk.bits_per_sample = 13;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[WAV] \"test.wav\": metadata check failed"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav, chunk_size_mismatch) {
  wav_fmt_chunk.bits_per_sample = 24;
  wav_fmt_chunk.avg_bytes_per_sec = 44100 * 3;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[WAV] \"test.wav\": `data` chunk size includes 1 "
                         "padding byte(s); got 16, expected 15"));

  ASSERT_EQ(3, frag.size());

  auto f1 = frag.span()[0];
  auto f2 = frag.span()[1];
  auto f3 = frag.span()[2];

  EXPECT_EQ("pcmaudio/metadata", catmgr.category_name(f1.category().value()));
  EXPECT_EQ("pcmaudio/waveform", catmgr.category_name(f2.category().value()));
  EXPECT_EQ(15, f2.size());
  EXPECT_EQ("pcmaudio/metadata", catmgr.category_name(f3.category().value()));
  EXPECT_EQ(1, f3.size());
}

TEST_F(pcmaudio_error_test_wav, unexpected_file_size) {
  wav_file_hdr.size -= 4;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr(
          "[WAV] \"test.wav\": unexpected file size: 48 (expected 52)"));

  ASSERT_EQ(2, frag.size());

  auto f1 = frag.span()[0];
  auto f2 = frag.span()[1];

  EXPECT_EQ("pcmaudio/metadata", catmgr.category_name(f1.category().value()));
  EXPECT_EQ(44, f1.size());
  EXPECT_EQ("pcmaudio/waveform", catmgr.category_name(f2.category().value()));
  EXPECT_EQ(16, f2.size());
}

TEST_F(pcmaudio_error_test_wav64, no_error) {
  auto builder = build_file();
  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(104, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_wav64, no_error_alignment) {
  wav_file_hdr.size = 128;
  wav_fmt_chunk_hdr.size = 42;

  pcmfile_builder builder;
  builder.add(wav_file_hdr);
  builder.add(wav_fmt_chunk_hdr);
  builder.add(wav_fmt_chunk, 18);
  builder.add_bytes(6, 0); // pad for alignment
  builder.add(wav_data_chunk_hdr);
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(112, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_wav64, truncated_file) {
  auto builder = build_file();
  builder.data.resize(builder.data.size() - 41);

  auto frag = categorize(builder);

  ASSERT_EQ(2, logger.get_log().size());

  EXPECT_THAT(
      logger.get_log()[0].output,
      testing::HasSubstr("[WAV64] \"test.w64\": unexpected file size: 120 "
                         "(expected 79)"));

  EXPECT_THAT(
      logger.get_log()[1].output,
      testing::HasSubstr("[WAV64] \"test.w64\": unexpected end of file"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_wav64, invalid_chunk_size) {
  wav_fmt_chunk_hdr.size = 8;

  auto builder = build_file();
  auto frag = categorize(builder);

  ASSERT_EQ(1, logger.get_log().size());

  EXPECT_THAT(
      logger.get_log()[0].output,
      testing::HasSubstr("[WAV64] \"test.w64\": invalid chunk size: 8"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_aiff, no_error) {
  auto builder = build_file();
  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(54, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_aiff, unexpected_second_comm_chunk) {
  aiff_file_hdr.size += 26;

  pcmfile_builder builder;
  builder.add(as_big_endian(aiff_file_hdr));
  builder.add(as_big_endian(aiff_comm_chunk_hdr));
  builder.add(as_big_endian(aiff_comm_chunk));
  builder.add(as_big_endian(aiff_comm_chunk_hdr));
  builder.add(as_big_endian(aiff_comm_chunk));
  builder.add(as_big_endian(aiff_ssnd_chunk_hdr));
  builder.add(as_big_endian(aiff_ssnd_chunk));
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[AIFF] \"test.aiff\": unexpected second `COMM` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_aiff, missing_comm_chunk) {
  aiff_file_hdr.size -= 26;

  pcmfile_builder builder;
  builder.add(as_big_endian(aiff_file_hdr));
  builder.add(as_big_endian(aiff_ssnd_chunk_hdr));
  builder.add(as_big_endian(aiff_ssnd_chunk));
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr(
          "[AIFF] \"test.aiff\": got `SSND` chunk without `COMM` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_aiff, ssnd_invalid_chunk_size) {
  aiff_ssnd_chunk_hdr.size -= 1;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[AIFF] \"test.aiff\": `SSND` invalid chunk size: 23"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, no_error) {
  auto builder = build_file();
  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(68, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_caf, no_error_unkown_data_size) {
  caff_data_chunk_hdr.size = -1;

  auto builder = build_file();
  auto frag = categorize(builder);

  EXPECT_TRUE(logger.empty());

  ASSERT_EQ(2, frag.size());

  auto const& first = frag.span()[0];
  auto const& second = frag.span()[1];
  EXPECT_EQ("pcmaudio/metadata",
            catmgr.category_name(first.category().value()));
  EXPECT_EQ(68, first.size());
  EXPECT_EQ("pcmaudio/waveform",
            catmgr.category_name(second.category().value()));
  EXPECT_EQ(16, second.size());
  EXPECT_EQ(builder.size(), first.size() + second.size());
}

TEST_F(pcmaudio_error_test_caf, unsupported_version_or_flags) {
  caff_file_hdr.version = 2;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": unsupported file version/flags: 2/0"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unexpected_second_desc_chunk) {
  pcmfile_builder builder;
  builder.add(as_big_endian(caff_file_hdr));
  builder.add(as_big_endian(caff_format_chunk_hdr));
  builder.add(as_big_endian(caff_format_chunk));
  builder.add(as_big_endian(caff_format_chunk_hdr));
  builder.add(as_big_endian(caff_format_chunk));
  builder.add(as_big_endian(caff_data_chunk_hdr));
  builder.add(as_big_endian(caff_data_chunk));
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[CAF] \"test.caf\": unexpected second `desc` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, missing_desc_chunk) {
  pcmfile_builder builder;
  builder.add(as_big_endian(caff_file_hdr));
  builder.add(as_big_endian(caff_data_chunk_hdr));
  builder.add(as_big_endian(caff_data_chunk));
  builder.add_bytes(16, 42);

  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": got `data` chunk without `desc` chunk"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unexpected_desc_chunk_size) {
  caff_format_chunk_hdr.size += 1;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[CAF] \"test.caf\": unexpected size for `desc` "
                         "chunk: 33 (expected 32)"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unsupported_format) {
  caff_format_chunk.format_id[0] = 'y';

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr("[CAF] \"test.caf\": unsupported `ypcm` format"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unsupported_floating_point) {
  caff_format_chunk.format_flags = 1;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": floating point format not supported"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unsupported_frames_per_packet) {
  caff_format_chunk.frames_per_packet = 2;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": unsupported frames per packet: 2"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, bytes_per_packet_zero) {
  caff_format_chunk.bytes_per_packet = 0;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": bytes per packet must not be zero"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, bytes_per_packet_out_of_range) {
  caff_format_chunk.bytes_per_packet = 5;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr(
                  "[CAF] \"test.caf\": bytes per packet out of range: 5"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, unsupported_packet_size) {
  caff_format_chunk.channels_per_frame = 4;
  caff_format_chunk.bytes_per_packet = 10;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(
      log.front().output,
      testing::HasSubstr(
          "[CAF] \"test.caf\": unsupported packet size: 10 (4 channels)"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, metadata_check_failed1) {
  caff_format_chunk.channels_per_frame = 1;
  caff_format_chunk.channels_per_frame = 4;
  caff_format_chunk.bytes_per_packet = 4;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[CAF] \"test.caf\": metadata check failed"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, metadata_check_failed2) {
  caff_format_chunk.channels_per_frame = 2;
  caff_format_chunk.bits_per_channel = 8;
  caff_format_chunk.bytes_per_packet = 4;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[CAF] \"test.caf\": metadata check failed"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, metadata_check_failed3) {
  caff_format_chunk.channels_per_frame = 1;
  caff_format_chunk.bits_per_channel = 24;
  caff_format_chunk.bytes_per_packet = 2;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[CAF] \"test.caf\": metadata check failed"));

  EXPECT_EQ(0, frag.size());
}

TEST_F(pcmaudio_error_test_caf, metadata_check_failed4) {
  caff_format_chunk.channels_per_frame = 1;
  caff_format_chunk.bits_per_channel = 32;
  caff_format_chunk.bytes_per_packet = 3;

  auto builder = build_file();
  auto frag = categorize(builder);
  auto const& log = logger.get_log();

  ASSERT_EQ(1, log.size());

  EXPECT_THAT(log.front().output,
              testing::HasSubstr("[CAF] \"test.caf\": metadata check failed"));

  EXPECT_EQ(0, frag.size());
}
