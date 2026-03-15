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

find_package(range-v3 ${RANGE_V3_REQUIRED_VERSION} CONFIG)

if(NOT range-v3_FOUND)
  FetchContent_Declare(
    range-v3
    GIT_REPOSITORY ${RANGE_V3_GIT_REPO}
    GIT_TAG ${RANGE_V3_PREFERRED_VERSION}
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(range-v3)
endif()
