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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

namespace dwarfs {

class logger;

namespace reader {

class filesystem_v2;

} // namespace reader

namespace writer {

class category_resolver;
class filesystem_writer;

} // namespace writer

namespace utility {

struct rewrite_options;

void rewrite_filesystem(logger& lgr, dwarfs::reader::filesystem_v2 const& fs,
                        dwarfs::writer::filesystem_writer& writer,
                        dwarfs::writer::category_resolver const& cat_resolver,
                        rewrite_options const& opts);

} // namespace utility

} // namespace dwarfs
