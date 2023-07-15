#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <fmt/format.h>

#include <FLAC++/encoder.h>

using namespace FLAC;

class MyStream : public Encoder::Stream {
 public:
  ::FLAC__StreamEncoderReadStatus read_callback(FLAC__byte buffer[], size_t *bytes) override {
    ::memcpy(buffer, data_.data() + pos_, *bytes);
    return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
  }

  ::FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[], size_t bytes, uint32_t, uint32_t) override {
    size_t end = pos_ + bytes;
    if (data_.size() < end) {
      data_.resize(end);
    }
    ::memcpy(data_.data() + pos_, buffer, bytes);
    pos_ += bytes;
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
  }

  ::FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset) override {
    pos_ = absolute_byte_offset;
    return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
  }

  ::FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *absolute_byte_offset) override {
    *absolute_byte_offset = pos_;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
  }

  void write(std::ostream& os) {
    os.write(reinterpret_cast<char*>(data_.data()), data_.size());
  }

 private:
  std::vector<uint8_t> data_;
  size_t pos_{0};
};

int main() {
  MyStream stream;

  if (!stream.is_valid()) {
    throw std::runtime_error(fmt::format("invalid stream: {}", stream.get_state().as_cstring()));
  }

  stream.set_streamable_subset(false);
  stream.set_channels(2);
  stream.set_bits_per_sample(16);
  stream.set_sample_rate(44100);
  stream.set_compression_level(8);
  stream.set_do_qlp_coeff_prec_search(true);
  stream.set_do_exhaustive_model_search(true);

  if (stream.init() != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    throw std::runtime_error(fmt::format("init: {}", stream.get_state().as_cstring()));
  }

  std::vector<int32_t> samples {
    0, 0, 0, 0
  };

  auto r = stream.process_interleaved(samples.data(), samples.size() / 2);

  if (!r) {
    throw std::runtime_error(fmt::format("failed to process interleaved samples: {}", stream.get_state().as_cstring()));
  }

  r = stream.finish();

  if (!r) {
    throw std::runtime_error("failed to finsh stream");
  }

  stream.write(std::cout);

  return 0;
}
