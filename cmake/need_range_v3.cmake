#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# dwarfs is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# dwarfs is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# dwarfs.  If not, see <https://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.28.0)

find_package(range-v3 ${RANGE_V3_REQUIRED_VERSION} CONFIG)

if(NOT range-v3_FOUND)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY ${RANGE_V3_GIT_REPO}
    GIT_TAG ${RANGE_V3_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(range-v3)
endif()
