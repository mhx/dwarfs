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

namespace cpp2 dwarfs

// It is actually ok to change the values of the enumerators,
// to add new enumerators, or to remove existing enumerators.
// However, *never* change the name of an enumerator, because
// the stringified name is used to serialize feature sets in
// the metadata. Also, *never* reuse an enumerator name, for
// the same reason. Be extra careful when removing enumerators,
// as this will break compatibility with older metadata using
// the feature defined by the removed enumerator.
enum feature {
  // There are no features yet :-)
}
