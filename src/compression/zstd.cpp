/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include <zstd.h>

#include <fmt/format.h>

#include <dwarfs/bits.h>
#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/match.h>
#include <dwarfs/option_map.h>

#include "base.h"

#if ZSTD_VERSION_MAJOR > 1 ||                                                  \
    (ZSTD_VERSION_MAJOR == 1 && ZSTD_VERSION_MINOR >= 4)
#define ZSTD_MIN_LEVEL ZSTD_minCLevel()
#else
#define ZSTD_MIN_LEVEL 1
#endif

namespace dwarfs {

namespace {

using namespace std::string_view_literals;

constexpr auto kCompressionLevelOpt = "level"sv;
constexpr auto kEnableLdmOpt = "long"sv;

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
constexpr std::array kZstdExtraParams{
    std::pair{ZSTD_c_windowLog, "wlog"sv},
    std::pair{ZSTD_c_hashLog, "hlog"sv},
    std::pair{ZSTD_c_chainLog, "clog"sv},
    std::pair{ZSTD_c_searchLog, "slog"sv},
    std::pair{ZSTD_c_minMatch, "mml"sv},
    std::pair{ZSTD_c_targetLength, "tlen"sv},
    std::pair{ZSTD_c_strategy, "strat"sv},
    std::pair{ZSTD_c_ldmHashLog, "lhlog"sv},
    std::pair{ZSTD_c_ldmMinMatch, "lmml"sv},
    std::pair{ZSTD_c_ldmBucketSizeLog, "lblog"sv},
    std::pair{ZSTD_c_ldmHashRateLog, "lhrlog"sv},
};

using extra_params_type =
    std::array<std::optional<int>, kZstdExtraParams.size()>;

constexpr extra_params_type get_extra_params(option_map& om) {
  extra_params_type result{};

  for (size_t i = 0; i < kZstdExtraParams.size(); ++i) {
    result[i] = om.get_optional<int>(kZstdExtraParams[i].second);
  }

  return result;
}
#endif

constexpr std::string_view get_param_name(int const param) {
  if (param == ZSTD_c_compressionLevel) {
    return kCompressionLevelOpt;
  }

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
  auto const it = std::ranges::find(
      kZstdExtraParams, param, &decltype(kZstdExtraParams)::value_type::first);

  if (it != kZstdExtraParams.end()) {
    return it->second;
  }
#endif

  return "unknown"sv;
}

#ifndef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS

constexpr double kZstdMemUsageFactor = 37664;
constexpr double kZstdMemUsageBase = 1.0001493955061902;
constexpr std::array<uint16_t, 23 * 21> kZstdMemUsage{
    692,   2878,  5843,  9456,  13517, 17847, 19216, 21322, 22223, 23726, 23726,
    23726, 23726, 23726, 23726, 23726, 23726, 23726, 23726, 23726, 23726, 0,
    1856,  4500,  7862,  11758, 10519, 13984, 17948, 18335, 18335, 18335, 18335,
    18335, 18335, 18335, 18335, 18335, 18335, 18335, 18335, 18335, 0,     1856,
    4500,  7862,  11758, 13420, 15858, 19049, 19049, 20282, 20282, 20282, 20282,
    20282, 20282, 20282, 20282, 20282, 20282, 20282, 20282, 692,   2878,  5843,
    9456,  13517, 17847, 19216, 21322, 22223, 23726, 23726, 23726, 23726, 23726,
    23726, 23726, 23726, 23726, 23726, 23726, 23726, 692,   2878,  5843,  9456,
    11758, 17847, 22327, 24952, 23016, 28384, 28384, 28384, 28384, 28384, 28384,
    28384, 28384, 28384, 28384, 28384, 28384, 692,   2878,  5843,  9456,  11758,
    16984, 21442, 23016, 25989, 29608, 29608, 29608, 29608, 29608, 29608, 29608,
    29608, 29608, 29608, 29608, 29608, 692,   2878,  5843,  9456,  11758, 16984,
    21442, 23016, 29608, 29608, 29608, 29608, 29608, 29608, 29608, 29608, 29608,
    29608, 29608, 29608, 29608, 692,   2878,  5843,  9456,  11758, 16984, 21442,
    23016, 29608, 33672, 33672, 33672, 33672, 33672, 33672, 33672, 33672, 33672,
    33672, 33672, 33672, 692,   2878,  5843,  9456,  11758, 16984, 21442, 23016,
    29608, 33672, 33672, 33672, 33672, 33672, 33672, 33672, 33672, 33672, 33672,
    33672, 33672, 1318,  3764,  6960,  10742, 13517, 16984, 21442, 23016, 29608,
    33672, 38005, 38005, 38005, 38005, 38005, 38005, 38005, 38005, 38005, 38005,
    38005, 1318,  3764,  6960,  10742, 13517, 16984, 21442, 23016, 29608, 33672,
    38005, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485,
    11011, 11671, 12824, 14663, 16347, 19298, 23809, 24952, 30642, 33672, 38005,
    42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 42485, 11182,
    11979, 13332, 15421, 17489, 19298, 23809, 26886, 32327, 33672, 38005, 42485,
    47044, 47044, 47044, 47044, 47044, 47044, 47044, 47044, 47044, 11182, 11979,
    13332, 15421, 17489, 20639, 24526, 27347, 30909, 36589, 41032, 45571, 45571,
    45571, 45571, 45571, 45571, 45571, 45571, 45571, 45571, 11182, 11979, 13332,
    15421, 18293, 21837, 25854, 29041, 31955, 36589, 41032, 45571, 48251, 48251,
    48251, 48251, 48251, 48251, 48251, 48251, 48251, 11182, 11979, 13332, 15421,
    18293, 21837, 25854, 29041, 31955, 36589, 41032, 45571, 50160, 50160, 50160,
    50160, 50160, 50160, 50160, 50160, 50160, 11182, 11979, 13332, 15421, 18293,
    21837, 25854, 29041, 33369, 36700, 41089, 45600, 45600, 45600, 45600, 45600,
    45600, 45600, 45600, 45600, 45600, 11182, 11979, 13332, 15421, 18293, 21837,
    25854, 29041, 33369, 36700, 41089, 45600, 48271, 48271, 48271, 48271, 48271,
    48271, 48271, 48271, 48271, 11182, 11979, 13332, 15421, 18293, 21837, 25854,
    29041, 33369, 37161, 41332, 45725, 48355, 48355, 48355, 48355, 48355, 48355,
    48355, 48355, 48355, 11182, 11979, 13332, 15421, 18293, 21837, 25854, 29041,
    33369, 37161, 41332, 45725, 48355, 51706, 51706, 51706, 51706, 51706, 51706,
    51706, 51706, 11182, 11979, 13332, 15421, 18293, 21837, 25854, 29041, 33369,
    37161, 41332, 45725, 50238, 52909, 56294, 56294, 56294, 56294, 56294, 56294,
    56294, 11182, 11979, 13332, 15421, 18293, 21837, 25854, 29041, 33369, 37161,
    41332, 45725, 50238, 54814, 57506, 60909, 60909, 60909, 60909, 60909, 60909,
    11182, 11979, 13332, 15421, 18293, 21837, 25854, 29041, 33369, 37161, 41332,
    45725, 50238, 54814, 59421, 62125, 65535, 65535, 65535, 65535, 65535,
};

constexpr double kZstdMemUsageFactorLong = 38372;
constexpr double kZstdMemUsageBaseLong = 1.0001580724165937;
constexpr std::array<uint16_t, 23 * 21> kZstdMemUsageLong{
    642,   2700,  5494,  8903,  12748, 16845, 18198, 20258, 21130, 22579, 22731,
    23024, 23572, 24543, 26127, 28448, 31477, 35057, 38997, 43148, 47413, 0,
    1750,  4244,  7418,  11113, 10104, 13431, 17212, 17642, 17808, 18126, 18718,
    19758, 21433, 23850, 26961, 30598, 34573, 38742, 43017, 47346, 0,     1750,
    4244,  7418,  11113, 12748, 15123, 18198, 18274, 19502, 19747, 20210, 21045,
    22443, 24557, 27402, 30850, 34708, 38813, 43053, 47364, 642,   2700,  5494,
    8903,  12748, 16845, 18198, 20258, 21130, 22579, 22731, 23024, 23572, 24543,
    26127, 28448, 31477, 35057, 38997, 43148, 47413, 642,   2700,  5494,  8923,
    11142, 16845, 21081, 23596, 21939, 26846, 26924, 27076, 27371, 27922, 28898,
    30489, 32817, 35852, 39437, 43380, 47532, 642,   2700,  5494,  8923,  11142,
    16069, 20286, 21853, 24641, 28045, 28173, 28421, 28891, 29738, 31152, 33283,
    36145, 39604, 43470, 47579, 51821, 642,   2699,  5493,  8923,  11142, 16069,
    20286, 21853, 27980, 28045, 28173, 28421, 28891, 29738, 31152, 33283, 36145,
    39604, 43470, 47579, 51821, 642,   2699,  5493,  8923,  11142, 16069, 20286,
    21853, 27980, 31805, 31876, 32015, 32286, 32794, 33703, 35203, 37432, 40381,
    43903, 47809, 51940, 642,   2699,  5493,  8923,  11142, 16069, 20286, 21853,
    27979, 31804, 31875, 32014, 32284, 32791, 33697, 35193, 37417, 40362, 43882,
    47786, 51916, 1224,  3525,  6566,  10155, 12815, 16069, 20286, 21853, 27979,
    31804, 35890, 35964, 36110, 36392, 36920, 37861, 39405, 41681, 44671, 48224,
    52148, 1224,  3525,  6566,  10155, 12815, 16069, 20286, 21853, 27979, 31804,
    35890, 40120, 40196, 40345, 40634, 41174, 42133, 43701, 46004, 49018, 52588,
    10312, 10939, 12046, 13808, 15436, 18255, 22521, 23693, 28996, 31804, 35890,
    40120, 40196, 40345, 40634, 41174, 42133, 43701, 46004, 49018, 52588, 10473,
    11228, 12523, 14516, 16499, 18255, 22521, 25464, 30555, 31804, 35890, 40120,
    44426, 44503, 44654, 44946, 45492, 46461, 48042, 50359, 53384, 10479, 11239,
    12541, 14542, 16536, 19498, 23185, 25889, 29243, 34573, 38766, 43053, 43148,
    43335, 43692, 44351, 45492, 47297, 49848, 53068, 56779, 10479, 11239, 12541,
    14542, 17282, 20613, 24417, 27453, 30210, 34573, 38766, 43053, 45617, 45744,
    45990, 46455, 47294, 48698, 50817, 53668, 57120, 10479, 11239, 12541, 14542,
    17282, 20613, 24417, 27453, 30210, 34573, 38766, 43053, 47389, 47485, 47673,
    48032, 48695, 49844, 51657, 54216, 57443, 10479, 11253, 12564, 14576, 17326,
    20652, 24460, 27506, 31550, 34676, 38819, 43080, 43175, 43360, 43716, 44371,
    45509, 47308, 49853, 53068, 56776, 10479, 11253, 12564, 14576, 17326, 20652,
    24460, 27506, 31550, 34676, 38819, 43080, 45635, 45762, 46007, 46471, 47307,
    48707, 50822, 53669, 57117, 10479, 11253, 12564, 14576, 17326, 20652, 24460,
    27506, 31624, 35120, 39054, 43201, 45716, 45841, 46083, 46541, 47368, 48755,
    50856, 53689, 57128, 10479, 11253, 12564, 14576, 17326, 20703, 24516, 27576,
    31624, 35203, 39143, 43294, 45841, 49058, 49348, 49891, 50855, 52429, 54739,
    57758, 61332, 10479, 11253, 12564, 14576, 17326, 20703, 24516, 27576, 31624,
    35203, 39143, 43294, 47558, 50146, 53395, 53687, 54234, 55204, 56786, 59104,
    62131, 10479, 11253, 12564, 14576, 17326, 20703, 24516, 27576, 31624, 35203,
    39143, 43294, 47558, 51882, 54490, 57756, 58049, 58598, 59571, 61157, 63480,
    10479, 11253, 12564, 14576, 17326, 20703, 24516, 27576, 31624, 35203, 39143,
    43294, 47558, 51882, 56237, 58855, 62129, 62423, 62973, 63947, 65535,
};

constexpr uint64_t
estimate_zstd_memory_usage(int const level, bool const enable_long,
                           uint64_t const data_size) {
  auto const l = std::clamp<int>(level, 0, 22);
  auto const s = std::clamp<int>(log2_bit_ceil(data_size), 10, 30) - 10;
  auto unpack = [index = l * 21 + s](auto const& table, auto const factor,
                                     auto const base) {
    return static_cast<uint64_t>(
        std::ceil(factor * std::pow(base, table[index])));
  };
  if (enable_long) {
    return unpack(kZstdMemUsageLong, kZstdMemUsageFactorLong,
                  kZstdMemUsageBaseLong);
  }
  return unpack(kZstdMemUsage, kZstdMemUsageFactor, kZstdMemUsageBase);
}

#endif

using cctx_ptr = std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)>;

cctx_ptr zstd_cctx_create() {
  auto cctx = cctx_ptr(ZSTD_createCCtx(), &ZSTD_freeCCtx);
  if (!cctx) {
    DWARFS_THROW(runtime_error, "zstd: failed to create compression context");
  }
  return cctx;
}

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
using cctx_params_ptr =
    std::unique_ptr<ZSTD_CCtx_params, decltype(&ZSTD_freeCCtxParams)>;

cctx_params_ptr zstd_cctx_params_create() {
  auto cp = cctx_params_ptr(ZSTD_createCCtxParams(), &ZSTD_freeCCtxParams);
  if (!cp) {
    DWARFS_THROW(runtime_error,
                 "zstd: failed to create compression context parameters");
  }
  return cp;
}
#endif

template <typename T>
concept zstd_param_value = std::is_integral_v<T> ||
                           (std::is_enum_v<T> && std::is_convertible_v<T, int>);

template <typename Func, typename... Args>
auto zstd_checked(std::string_view message, Func&& func, Args&&... args) {
  auto r = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
  if (ZSTD_isError(r)) {
    DWARFS_THROW(runtime_error, fmt::format("failed to {} (zstd error: {})",
                                            message, ZSTD_getErrorName(r)));
  }
  return r;
}

ZSTD_bounds zstd_checked_bounds(ZSTD_cParameter param) {
  auto const bounds = ZSTD_cParam_getBounds(param);
  if (ZSTD_isError(bounds.error)) {
    DWARFS_THROW(
        runtime_error,
        fmt::format("failed to get bounds for parameter '{}': (zstd error: {})",
                    get_param_name(param), ZSTD_getErrorName(bounds.error)));
  }
  return bounds;
}

class zstd_block_compressor final : public block_compressor::impl {
 public:
  static std::vector<std::string> options() {
    std::vector<std::string> opts;

    auto add_bounded_option = [&opts](ZSTD_cParameter param,
                                      std::string_view name) {
      auto const bounds = zstd_checked_bounds(param);
      auto const value_str =
          fmt::format("[{}..{}]", bounds.lowerBound, bounds.upperBound);
      opts.push_back(fmt::format("{}={}", name, value_str));
    };

    add_bounded_option(ZSTD_c_compressionLevel, kCompressionLevelOpt);
    opts.emplace_back(kEnableLdmOpt); // similar to zstd's `--long` option

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
    for (auto const& [param, name] : kZstdExtraParams) {
      add_bounded_option(param, name);
    }
#endif

    return opts;
  }

  explicit zstd_block_compressor(option_map& om)
      : level_{om.get<int>(kCompressionLevelOpt, ZSTD_maxCLevel())}
      , enable_ldm_{om.get<bool>(kEnableLdmOpt)}
#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
      , extra_params_{get_extra_params(om)}
#endif
  {
  }

  zstd_block_compressor(zstd_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zstd_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* metadata) const override;

  compression_type type() const override { return compression_type::ZSTD; }

  std::string describe() const override {
    return fmt::format("zstd [level={}]", level_);
  }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

  size_t estimate_memory_usage(size_t data_size) const override {
#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
    auto cp = zstd_cctx_params_create();
    set_compression_parameters(cp.get(), data_size);
    auto const size =
        zstd_checked("estimate memory usage",
                     ZSTD_estimateCCtxSize_usingCCtxParams, cp.get());
    return size + data_size;
#else
    return estimate_zstd_memory_usage(level_, enable_ldm_, data_size) +
           data_size;
#endif
  }

 private:
  using cctx_param_settable_ptr = std::variant<
#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
      ZSTD_CCtx_params*,
#endif
      ZSTD_CCtx*>;

  template <zstd_param_value T>
  void set_cctx_param(cctx_param_settable_ptr sp, ZSTD_cParameter param,
                      T const& value) const {
    auto const r =
        sp | match{
#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
                 [&](ZSTD_CCtx_params* p) {
                   return ZSTD_CCtxParams_setParameter(p, param, value);
                 },
#endif
                 [&](ZSTD_CCtx* p) {
                   return ZSTD_CCtx_setParameter(p, param, value);
                 }};

    if (ZSTD_isError(r)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format(
              "failed to set compression parameter '{}': (zstd error: {})",
              get_param_name(param), ZSTD_getErrorName(r)));
    }
  }

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
  template <zstd_param_value T>
  void set_cctx_param_opt(cctx_param_settable_ptr sp, ZSTD_cParameter param,
                          std::optional<T> const& optval) const {
    if (optval.has_value()) {
      set_cctx_param(sp, param, *optval);
    }
  }
#endif

  void set_compression_parameters(cctx_param_settable_ptr sp,
                                  uint64_t data_size) const {
#ifdef ZSTD_STATIC_LINKING_ONLY
    set_cctx_param(sp, ZSTD_c_srcSizeHint,
                   data_size < std::numeric_limits<int>::max()
                       ? static_cast<int>(data_size)
                       : ZSTD_CONTENTSIZE_UNKNOWN);
#endif
    set_cctx_param(sp, ZSTD_c_compressionLevel, level_);

    if (enable_ldm_) {
      set_cctx_param(sp, ZSTD_c_enableLongDistanceMatching, 1);
      set_cctx_param(sp, ZSTD_c_windowLog, log2_bit_ceil(data_size));
    }

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
    for (size_t i = 0; i < kZstdExtraParams.size(); ++i) {
      set_cctx_param_opt(sp, kZstdExtraParams[i].first, extra_params_[i]);
    }
#endif
  }

  int const level_;
  bool const enable_ldm_;

#ifdef DWARFS_ZSTD_SUPPORTS_ESTIMATE_SIZE_BY_CCTX_PARAMS
  extra_params_type const extra_params_;
#endif
};

shared_byte_buffer
zstd_block_compressor::compress(shared_byte_buffer const& data,
                                std::string const* /*metadata*/) const {
  auto compressed = malloc_byte_buffer::create(); // TODO: make configurable
  compressed.resize(ZSTD_compressBound(data.size()));
  auto cctx = zstd_cctx_create();
  set_compression_parameters(cctx.get(), data.size());
  auto const size = zstd_checked("compress block data", ZSTD_compress2,
                                 cctx.get(), compressed.data(),
                                 compressed.size(), data.data(), data.size());
  cctx.reset();
  if (size >= data.size()) {
    throw bad_compression_ratio_error();
  }
  compressed.resize(size);
  compressed.shrink_to_fit();
  return compressed.share();
}

class zstd_block_decompressor final : public block_decompressor_base {
 public:
  zstd_block_decompressor(std::span<uint8_t const> data)
      : data_(data)
      , uncompressed_size_(ZSTD_getFrameContentSize(data.data(), data.size())) {
    switch (uncompressed_size_) {
    case ZSTD_CONTENTSIZE_UNKNOWN:
      DWARFS_THROW(runtime_error, "ZSTD content size unknown");
      break;

    case ZSTD_CONTENTSIZE_ERROR:
      DWARFS_THROW(runtime_error, "ZSTD content size error");
      break;

    default:
      break;
    }
  }

  compression_type type() const override { return compression_type::ZSTD; }

  bool decompress_frame(size_t /*frame_size*/) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);
    auto rv = ZSTD_decompress(decompressed_.data(), decompressed_.size(),
                              data_.data(), data_.size());

    if (ZSTD_isError(rv)) {
      decompressed_.clear();
      error_ = fmt::format("failed to decompress frame (zstd error: {})",
                           ZSTD_getErrorName(rv));
      DWARFS_THROW(runtime_error, error_);
    }

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  std::span<uint8_t const> data_;
  unsigned long long const uncompressed_size_;
  std::string error_;
};

template <typename Base>
class zstd_compression_info : public Base {
 public:
  static constexpr compression_type type{compression_type::ZSTD};

  std::string_view name() const override { return "zstd"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("ZSTD compression (libzstd {})", ::ZSTD_versionString())};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libzstd-{}", ::ZSTD_versionString())};
  }
};

class zstd_compressor_factory final
    : public zstd_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<zstd_block_compressor>(om);
  }

 private:
  std::vector<std::string> const options_{zstd_block_compressor::options()};
};

class zstd_decompressor_factory final
    : public zstd_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<zstd_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(zstd_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(zstd_decompressor_factory)

} // namespace dwarfs
