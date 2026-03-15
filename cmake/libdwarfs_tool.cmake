#
# Copyright (c) Marcus Holland-Moritz
#
# This file is part of dwarfs.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.28.0)

add_library(
  dwarfs_tool OBJECT

  tools/src/tool/iolayer.cpp
  tools/src/tool/main_adapter.cpp
  tools/src/tool/safe_main.cpp
  tools/src/tool/sys_char.cpp
  tools/src/tool/sysinfo.cpp
  tools/src/tool/tool.cpp
)

# TODO: Try enabling folly memcpy also for ARM if we figure out how that works :-)
if(STATIC_BUILD_DO_NOT_USE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  enable_language(ASM)
  target_sources(dwarfs_tool PRIVATE folly-memcpy/folly/memcpy.S)
  set_property(SOURCE folly-memcpy/folly/memcpy.S APPEND PROPERTY COMPILE_OPTIONS "-x" "assembler-with-cpp" "-D__AVX2__")
  target_link_options(dwarfs_tool PUBLIC -Wl,--wrap=memcpy)
  target_compile_definitions(dwarfs_tool PRIVATE -DDWARFS_USE_FOLLY_MEMCPY)
endif()

if(WITH_MAN_OPTION)
  target_sources(dwarfs_tool PRIVATE
    tools/src/tool/pager.cpp
    tools/src/tool/render_manpage.cpp
  )
endif()

target_link_libraries(dwarfs_tool PUBLIC dwarfs_common Boost::program_options)
target_include_directories(dwarfs_tool PUBLIC tools/include)

if(USE_JEMALLOC AND JEMALLOC_FOUND)
  target_link_libraries(dwarfs_tool PRIVATE PkgConfig::JEMALLOC)
  target_compile_definitions(dwarfs_tool PRIVATE DWARFS_USE_JEMALLOC)
endif()

if(USE_MIMALLOC AND mimalloc_FOUND)
  target_link_libraries(dwarfs_tool PRIVATE mimalloc-static)
  target_compile_definitions(dwarfs_tool PRIVATE DWARFS_USE_MIMALLOC)
endif()

target_compile_definitions(
  dwarfs_tool PRIVATE DWARFS_BUILD_ID="${CMAKE_SYSTEM_PROCESSOR} ${CMAKE_SYSTEM_NAME} using ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

list(APPEND LIBDWARFS_OBJECT_TARGETS
  dwarfs_tool
)
