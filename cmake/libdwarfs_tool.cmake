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

add_library(
  dwarfs_tool OBJECT

  tools/src/tool/iolayer.cpp
  tools/src/tool/main_adapter.cpp
  tools/src/tool/safe_main.cpp
  tools/src/tool/sys_char.cpp
  tools/src/tool/tool.cpp
)

if(WITH_MAN_OPTION)
  target_sources(dwarfs_tool PRIVATE
    tools/src/tool/pager.cpp
    tools/src/tool/render_manpage.cpp
  )
endif()

target_link_libraries(dwarfs_tool PUBLIC dwarfs_common)
target_include_directories(dwarfs_tool PUBLIC tools/include)

if(USE_JEMALLOC AND JEMALLOC_FOUND)
  target_link_libraries(dwarfs_tool PRIVATE PkgConfig::JEMALLOC)
  target_compile_definitions(dwarfs_tool PRIVATE DWARFS_USE_JEMALLOC)
endif()

target_compile_definitions(
  dwarfs_tool PRIVATE DWARFS_BUILD_ID="${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SYSTEM}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

list(APPEND LIBDWARFS_OBJECT_TARGETS
  dwarfs_tool
)
