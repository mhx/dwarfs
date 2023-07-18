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
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <lz4.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace po = boost::program_options;

namespace {

constexpr std::string_view const INCOMPRESSIBLE_CATEGORY{"incompressible"};

struct incompressible_categorizer_config {
  size_t min_input_size;
  double max_ratio_size;
  double max_ratio_blocks;
  int lz4_acceleration;
};

template <typename LoggerPolicy>
class incompressible_categorizer_job_ : public sequential_categorizer_job {
 public:
  static constexpr size_t const block_size{1024 * 1024};

  incompressible_categorizer_job_(logger& lgr,
                                  incompressible_categorizer_config const& cfg,
                                  std::filesystem::path const& path,
                                  size_t total_size,
                                  category_mapper const& mapper)
      : LOG_PROXY_INIT(lgr)
      , cfg_{cfg}
      , path_{path}
      , mapper_{mapper} {
    input_.reserve(total_size < block_size ? total_size : block_size);
    state_ = ::malloc(LZ4_sizeofState());
  }

  ~incompressible_categorizer_job_() { ::free(state_); }

  void add(std::span<uint8_t const> data) override {
    while (!data.empty()) {
      auto part_size = input_.size() + data.size() <= block_size
                           ? data.size()
                           : block_size - input_.size();
      add_input(data.first(part_size));
      data = data.subspan(part_size);
    }
  }

  inode_fragments result() override {
    inode_fragments fragments;
    if (!input_.empty()) {
      compress();
    }
    LOG_TRACE << path_ << " -> blocks: " << incompressible_blocks_ << "/"
              << total_blocks_ << ", total compression ratio: "
              << fmt::format("{:.2f}%",
                             100.0 * total_output_size_ / total_input_size_);
    if (total_blocks_ > 0 &&
        (total_output_size_ >= cfg_.max_ratio_size * total_input_size_ ||
         incompressible_blocks_ >= cfg_.max_ratio_blocks * total_blocks_)) {
      fragments.emplace_back(
          fragment_category(mapper_(INCOMPRESSIBLE_CATEGORY)),
          total_input_size_);
    }
    return fragments;
  }

 private:
  void add_input(std::span<uint8_t const> data) {
    auto current_size = input_.size();
    assert(current_size + data.size() <= block_size);
    input_.resize(current_size + data.size());
    ::memcpy(&input_[current_size], data.data(), data.size());
    if (input_.size() == block_size) {
      compress();
    }
  }

  void compress() {
    total_input_size_ += input_.size();

    output_.resize(::LZ4_compressBound(input_.size()));

    auto rv = ::LZ4_compress_fast_extState(
        state_, reinterpret_cast<char*>(input_.data()),
        reinterpret_cast<char*>(output_.data()), input_.size(), output_.size(),
        cfg_.lz4_acceleration);

    if (rv == 0) {
      DWARFS_THROW(runtime_error,
                   "unexpected error in LZ4_compress_fast_extState");
    }

    total_output_size_ += rv;
    ++total_blocks_;

    if (rv >= static_cast<int>(cfg_.max_ratio_size * input_.size())) {
      ++incompressible_blocks_;
    }

    input_.clear();
  }

  LOG_PROXY_DECL(LoggerPolicy);
  void* state_;
  std::vector<uint8_t> input_;
  std::vector<uint8_t> output_;
  size_t total_input_size_{0};
  size_t total_output_size_{0};
  size_t total_blocks_{0};
  size_t incompressible_blocks_{0};
  incompressible_categorizer_config const& cfg_;
  std::filesystem::path const& path_;
  category_mapper const& mapper_;
};

class incompressible_categorizer_ final : public sequential_categorizer {
 public:
  incompressible_categorizer_(logger& lgr,
                              incompressible_categorizer_config const& cfg);

  std::span<std::string_view const> categories() const override;
  std::unique_ptr<sequential_categorizer_job>
  job(std::filesystem::path const& path, size_t total_size,
      category_mapper const& mapper) const override;
  bool is_single_fragment() const override { return true; }

 private:
  logger& lgr_;
  incompressible_categorizer_config const config_;
};

incompressible_categorizer_::incompressible_categorizer_(
    logger& lgr, incompressible_categorizer_config const& cfg)
    : lgr_{lgr}
    , config_{cfg} {}

std::span<std::string_view const>
incompressible_categorizer_::categories() const {
  static constexpr std::array const s_categories{
      INCOMPRESSIBLE_CATEGORY,
  };
  return s_categories;
}

std::unique_ptr<sequential_categorizer_job>
incompressible_categorizer_::job(std::filesystem::path const& path,
                                 size_t total_size,
                                 category_mapper const& mapper) const {
  if (total_size < config_.min_input_size) {
    return nullptr;
  }

  return make_unique_logging_object<sequential_categorizer_job,
                                    incompressible_categorizer_job_,
                                    logger_policies>(lgr_, config_, path,
                                                     total_size, mapper);
}

class incompressible_categorizer_factory : public categorizer_factory {
 public:
  incompressible_categorizer_factory()
      : opts_{std::make_shared<po::options_description>(
            "Incompressible categorizer options")} {
    static constexpr double const default_ratio{0.99};
    auto const default_ratio_str{fmt::format("{:.2f}", default_ratio)};
    // clang-format off
    opts_->add_options()
      ("incompressible-min-input-size",
          po::value<size_t>(&cfg_.min_input_size)->default_value(256),
          "minimum file size in bytes to check for incompressibility")
      ("incompressible-max-size-ratio",
          po::value<double>(&cfg_.max_ratio_size)
            ->default_value(default_ratio, default_ratio_str),
          "LZ4 compression ratio above files are considered incompressible")
      ("incompressible-max-blocks-ratio",
          po::value<double>(&cfg_.max_ratio_blocks)
            ->default_value(default_ratio, default_ratio_str),
          "ratio of incompressible LZ4 blocks above which the whole file"
          " is considered incompressible")
      ("incompressible-lz4-acceleration (1..65537)",
          po::value<int>(&cfg_.lz4_acceleration)->default_value(1),
          "LZ4 acceleration value")
      ;
    // clang-format on
  }

  std::string_view name() const override { return "incompressible"; }

  std::shared_ptr<po::options_description const> options() const override {
    return opts_;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/) const override {
    return std::make_unique<incompressible_categorizer_>(lgr, cfg_);
  }

 private:
  incompressible_categorizer_config cfg_;
  std::shared_ptr<po::options_description> opts_;
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(incompressible_categorizer_factory)

} // namespace dwarfs
