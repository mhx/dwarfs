#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fmt/format.h>

#include <range/v3/view/chunk.hpp>

#include "ricepp/byteswap.h"
#include "ricepp/ricepp.h"

namespace {

template <typename T>
std::span<T> map_file(char const* filename, size_t size = 0) {
  constexpr bool readonly = std::is_const_v<T>;

  auto fd = open(filename, readonly ? O_RDONLY : (O_RDWR | O_CREAT), 0644);
  if (fd == -1) {
    std::cerr << "Failed to open file " << filename << ": " << strerror(errno)
              << '\n';
    return {};
  }

  if constexpr (readonly) {
    if (size == 0) {
      struct stat st;
      if (fstat(fd, &st) == -1) {
        std::cerr << "Failed to stat file " << filename << ": "
                  << strerror(errno) << '\n';
        return {};
      }
      size = st.st_size / sizeof(T);
    }
  } else {
    if (ftruncate(fd, size * sizeof(T)) == -1) {
      std::cerr << "Failed to truncate file " << filename << ": "
                << strerror(errno) << '\n';
      return {};
    }
  }

  auto map = mmap(nullptr, size * sizeof(T),
                  readonly ? PROT_READ : (PROT_READ | PROT_WRITE),
                  readonly ? MAP_PRIVATE : MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    std::cerr << "Failed to mmap file " << filename << ": " << strerror(errno)
              << '\n';
    return {};
  }

  if (close(fd) == -1) {
    std::cerr << "Failed to close file " << filename << ": " << strerror(errno)
              << '\n';
    return {};
  }

  return {static_cast<T*>(map), size};
}

template <typename T>
void unmap_span(std::span<T> map) {
  if (munmap(static_cast<void*>(const_cast<std::decay_t<T>*>(map.data())),
             map.size() * sizeof(T)) == -1) {
    std::cerr << "Failed to munmap file: " << strerror(errno) << '\n';
  }
}

struct fits_info {
  unsigned pixel_bits{};
  unsigned component_count{};
  unsigned unused_lsb_count{};
  std::span<uint16_t const> imagedata;
};

std::string_view trim(std::string_view sv) {
  if (auto const pos = sv.find_first_not_of(' ');
      pos != std::string_view::npos) {
    sv.remove_prefix(pos);
  }
  if (auto const pos = sv.find_last_not_of(' ');
      pos != std::string_view::npos) {
    sv.remove_suffix(sv.size() - pos - 1);
  }
  return sv;
}

unsigned get_unused_lsb_count(std::span<uint16_t const> imagedata) {
  std::span<uint64_t const> data{
      reinterpret_cast<uint64_t const*>(imagedata.data()),
      (imagedata.size_bytes() + sizeof(uint64_t) - 1) / sizeof(uint64_t)};
  static constexpr uint64_t const lsb_mask = UINT64_C(0x0100010001000100);
  uint64_t bits = 0;
  for (auto d : data) {
    bits |= d;
    if (bits & lsb_mask) {
      return 0;
    }
  }
  uint16_t bits16 = ricepp::byteswap(
      (bits >> 48) | (bits >> 32) | (bits >> 16) | bits, std::endian::big);
  return std::countr_zero(bits16);
}

std::optional<fits_info> parse_fits(std::span<uint16_t const> fits) {
  std::span<char const> header{reinterpret_cast<char const*>(fits.data()),
                               fits.size_bytes()};

  fits_info fi;
  fi.component_count = 1;

  int pixel_bits = -1;
  int xdim = -1;
  int ydim = -1;

  for (auto row : header | ranges::views::chunk(80)) {
    std::string_view rv{row.begin(), row.end()};
    auto keyword = trim(rv.substr(0, 8));
    if (keyword == "COMMENT") {
      continue;
    }
    if (keyword == "END") {
      if (xdim == -1 || ydim == -1) {
        std::cerr << "Missing NAXIS1 or NAXIS2\n";
        return std::nullopt;
      }
      if (pixel_bits != 16) {
        std::cerr << "Not a 16-bit FITS file\n";
        return std::nullopt;
      }

      auto const header_frames =
          (std::distance(header.begin(), row.end()) + 2880 - 1) / 2880;
      fi.imagedata = {fits.data() + header_frames * 1440,
                      static_cast<size_t>(xdim * ydim)};
      fi.pixel_bits = static_cast<unsigned>(pixel_bits);
      auto t0 = std::chrono::steady_clock::now();
      fi.unused_lsb_count = get_unused_lsb_count(fi.imagedata);
      auto t1 = std::chrono::steady_clock::now();
      std::cout << fmt::format(
          "get_unused_lsb_count took {} us\n",
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
              .count());
      return fi;
    }
    if (rv[8] != '=') {
      continue;
    }
    auto value = rv.substr(9);
    if (auto pos = value.find('/'); pos != std::string_view::npos) {
      value.remove_suffix(value.size() - pos);
    }
    value = trim(value);

    if (keyword == "SIMPLE") {
      if (value != "T") {
        std::cerr << "Not a simple FITS file\n";
        return std::nullopt;
      }
    } else if (keyword == "BITPIX") {
      std::from_chars(value.data(), value.data() + value.size(), pixel_bits);
    } else if (keyword == "NAXIS") {
      if (value != "2") {
        std::cerr << "Not a 2D FITS file\n";
        return std::nullopt;
      }
    } else if (keyword == "NAXIS1") {
      std::from_chars(value.data(), value.data() + value.size(), xdim);
    } else if (keyword == "NAXIS2") {
      std::from_chars(value.data(), value.data() + value.size(), ydim);
    } else if (keyword == "BAYERPAT") {
      fi.component_count = 2;
    }
  }

  return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: " << argv[0] << " <input> <output> [<block-size>]\n";
    return 1;
  }

  size_t block_size = 128;
  if (argc == 4) {
    block_size = std::stoul(argv[3]);
  }

  using pixel_type = uint16_t;

  auto fits_input = map_file<pixel_type const>(argv[1]);
  if (fits_input.empty()) {
    std::cerr << "Failed to open input file\n";
    return 1;
  }

  auto fi = parse_fits(fits_input);

  if (!fi) {
    std::cerr << "Failed to parse FITS file\n";
    return 1;
  }

  std::cout << fmt::format("pixel_bits: {}\n", fi->pixel_bits);
  std::cout << fmt::format("component_count: {}\n", fi->component_count);
  std::cout << fmt::format("unused_lsb_count: {}\n", fi->unused_lsb_count);
  std::cout << fmt::format("imagedata.size(): {}\n", fi->imagedata.size());

  auto codec = ricepp::create_codec<pixel_type>({
      .block_size = block_size,
      .component_stream_count = fi->component_count,
      .byteorder = std::endian::big,
      .unused_lsb_count = fi->unused_lsb_count,
  });

  auto input = fi->imagedata;

  auto output = map_file<pixel_type>(argv[2], input.size());
  if (output.empty()) {
    std::cerr << "Failed to open output file\n";
    return 1;
  }

  std::vector<uint8_t> compressed;

  {
    auto t0 = std::chrono::steady_clock::now();

    compressed = codec->encode(input);

    auto t1 = std::chrono::steady_clock::now();

    std::cout << fmt::format(
        "compressing {} bytes to {} bytes ({:.2f}%) took {} ms ({:.1f} "
        "MiB/s)\n",
        input.size() * sizeof(pixel_type), compressed.size(),
        static_cast<double>(compressed.size()) /
            static_cast<double>(input.size() * sizeof(pixel_type)) * 100.0,
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        static_cast<double>(input.size() * sizeof(pixel_type)) /
            (1024.0 * 1024.0) /
            (std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                 .count() /
             1000000.0));
  }

  std::ofstream out{"ricepp.bin", std::ios::binary};
  out.write(reinterpret_cast<char const*>(compressed.data()),
            compressed.size());
  out.close();

  {
    auto t0 = std::chrono::steady_clock::now();

    codec->decode(output, compressed);

    auto t1 = std::chrono::steady_clock::now();

    std::cout << fmt::format(
        "decompression took {} ms ({:.1f} MiB/s)\n",
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        static_cast<double>(input.size() * sizeof(pixel_type)) /
            (1024.0 * 1024.0) /
            (std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                 .count() /
             1000000.0));
  }

  if (memcmp(input.data(), output.data(), input.size() * sizeof(pixel_type)) !=
      0) {
    std::cerr << "Decompressed data does not match original\n";
    return 1;
  }

  unmap_span(fits_input);
  unmap_span(output);

  return 0;
}
