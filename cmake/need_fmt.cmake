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

find_package(fmt ${LIBFMT_REQUIRED_VERSION} CONFIG)

if(NOT fmt_FOUND)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY ${LIBFMT_GIT_REPO}
    GIT_TAG ${LIBFMT_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(fmt)
endif()

# folly won't build on Windows with FMT_HEADER_ONLY
if(fmt_FOUND OR STATIC_BUILD_DO_NOT_USE OR WIN32)
  set(DWARFS_FMT_LIB fmt::fmt)
else()
  add_compile_definitions(FMT_HEADER_ONLY)
  include_directories(SYSTEM $<BUILD_INTERFACE:$<TARGET_PROPERTY:fmt::fmt-header-only,INTERFACE_INCLUDE_DIRECTORIES>>)
endif()
