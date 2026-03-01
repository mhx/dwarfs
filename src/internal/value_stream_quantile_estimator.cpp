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

// https://github.com/boostorg/accumulators/issues/49
#include <sstream>
#include <vector>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/extended_p_square_quantile.hpp>
#include <boost/accumulators/statistics/stats.hpp>

#include <dwarfs/internal/value_stream_quantile_estimator.h>

namespace dwarfs::internal {

namespace {

namespace acc = boost::accumulators;

class estimator_ : public value_stream_quantile_estimator::impl {
 public:
  using accumulator_t =
      acc::accumulator_set<double,
                           acc::stats<acc::tag::extended_p_square_quantile>>;

  explicit estimator_(std::span<double const> quantiles)
      : quantiles_{quantiles.begin(), quantiles.end()}
      , acc_{acc::extended_p_square_probabilities = quantiles_} {}

  void add(double value) override { acc_(value); }

  double quantile(double q) const override {
    return acc::quantile(acc_, acc::quantile_probability = q);
  }

 private:
  std::vector<double> const quantiles_;
  accumulator_t acc_;
};

} // namespace

value_stream_quantile_estimator::value_stream_quantile_estimator(
    std::initializer_list<double> quantiles)
    : impl_{std::make_unique<estimator_>(quantiles)} {}

value_stream_quantile_estimator::value_stream_quantile_estimator(
    std::span<double const> quantiles)
    : impl_{std::make_unique<estimator_>(quantiles)} {}

} // namespace dwarfs::internal
