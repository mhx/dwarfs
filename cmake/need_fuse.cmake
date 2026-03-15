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

if(WIN32)
  if(NOT WINFSP_PATH)
    set(WINFSP_PATH "C:/Program Files (x86)/WinFsp")
  endif()
  find_library(WINFSP winfsp-x64.lib "${WINFSP_PATH}/lib")
  if (NOT WINFSP)
    message(FATAL_ERROR "No WinFsp library found")
  endif()
else()
  pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.9.9)
  pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3>=3.10.5)

  if(NOT FUSE_FOUND AND NOT FUSE3_FOUND)
    message(FATAL_ERROR "No FUSE or FUSE3 library found")
  endif()
endif()
