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

find_package(blake3 ${BLAKE3_REQUIRED_VERSION} CONFIG)

if(NOT blake3_FOUND)
  set(ORIG_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_Declare(
    blake3
    GIT_REPOSITORY ${BLAKE3_GIT_REPO}
    GIT_TAG ${BLAKE3_PREFERRED_VERSION}
    SOURCE_SUBDIR c
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(blake3)
  set_target_properties(blake3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  set(BUILD_SHARED_LIBS ${ORIG_BUILD_SHARED_LIBS})
endif()
