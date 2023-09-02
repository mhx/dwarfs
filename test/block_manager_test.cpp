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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace dwarfs;

class block_interface
{
 public:
  virtual ~block_interface() = default;

  virtual size_t size() const = 0;
  // TODO: category?
};

class block_manager
{
 public:
  block_manager();
};


TEST(block_manager, deterministic_ordering) {
}

/*
- Order segmenters by size (largest first) and name
  - This is tricky for subcategories as these are
    not deterministic

- Assign segmenters in order to N worker threads

- Round-robin through segmenters:
  - First segmenter writes first block
  - Second writes second block, ...

- Each segmenter can queue blocks before writing, but
  subject to a global limit (block manager semaphore)

- The first segmenter to finish (i.e. with the smallest
  total output size) gets replaced by the next segmenter
  in order


- Lookback & max memory size must somehow be related:

  - total-lookback*block-size <= max-memory:
    - hold up to max-memory/block-size blocks
  - total-lookback*block-size > max-memory:
    - issue a warning
    - hold up to total-lookback blocks



- block manager is initialized with ordered list of categories
- block manager receives blocks from segmenters, along with the
  category, from different threads
- blocks manager can block individual segmenters if other
  segmenters need to catch up; need to make sure that we don't
  lock up
  - can we always prefer to schedule the segmenter that is
    furthest behind?
  - is it enough if each category can always "reserve" a "slot"
    in the semaphore?
  - or is this completely irrelevant because once the furthest
    behind segmenter produces a block, N-1 segmenters will be
    unblocked because their blocks can now also be written?
- since segmenters can advance ahead, we still need the block
  manager to keep track of a mapping between logical and physical
  blocks
*/
