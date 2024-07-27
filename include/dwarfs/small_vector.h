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

#pragma once

#include <boost/container/small_vector.hpp>

namespace dwarfs {

/*
Turns out boost's small_vector is faster on average than folly's.

----------------------------------------------------------------
                             int                  string
                       folly      boost      folly      boost
----------------------------------------------------------------
defaultCtor             28.90ns    13.20ns    30.53ns    14.60ns
sizeCtor(16)           205.65ns    36.78ns   289.87ns   110.44ns
sizeCtor(128)          420.11ns    38.47ns   872.85ns   598.27ns
sizeCtor(1024)           2.15us    77.12ns     4.50us     4.47us
fillCtor(16)           227.79ns   100.11ns    13.44us    13.23us
fillCtor(128)          457.34ns   387.14ns    17.44us    17.49us
fillCtor(1024)           2.33us     2.67us    46.35us    46.18us
reserve(16)            192.40ns    87.70ns   232.90ns    99.59ns
reserve(128)           191.61ns    87.51ns   248.49ns   117.94ns
reserve(1024)          230.65ns   108.48ns   266.32ns   122.62ns
insertFront(16)          3.08us     3.04us   126.98us   111.87us
insertFront(128)         3.08us     3.04us   128.31us   111.62us
insertFront(1024)        3.14us     3.10us   129.76us   109.75us
insertFront(10240)       3.72us     3.68us   153.54us   129.10us
insertFront(102400)      9.35us     9.30us   386.54us   332.21us
insertFront(1024000)   108.52us   109.90us     3.01ms     2.75ms
pushBack(16)            17.18ns     4.28ns    43.32ns    22.47ns
pushBack(128)           17.21ns     4.31ns    34.87ns    25.60ns
pushBack(1024)          17.23ns     4.26ns    31.76ns    21.53ns
pushBack(10240)         17.13ns     4.33ns    31.40ns    27.04ns
pushBack(102400)        17.24ns     4.34ns    42.96ns    27.41ns
pushBack(1024000)       18.77ns     6.00ns   100.87ns    88.54ns
----------------------------------------------------------------
*/

template <typename T, size_t N>
using small_vector = boost::container::small_vector<T, N>;

} // namespace dwarfs
