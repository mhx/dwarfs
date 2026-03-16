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

function(add_manpage_source markdown)
  set(_options)
  set(_oneValueArgs NAME OUTPUT)
  set(_multiValueArgs)
  cmake_parse_arguments(_MANPAGE "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  find_program(_PYTHON_EXE python3 python)
  if(NOT _PYTHON_EXE)
    find_package(Python3 REQUIRED)
    set(_PYTHON_EXE "${Python3_EXECUTABLE}")
  endif()

  set(_MANPAGE_GENERATOR "${CMAKE_SOURCE_DIR}/cmake/render_manpage.py")

  add_custom_command(
    OUTPUT "${_MANPAGE_OUTPUT}"
    COMMAND "${_PYTHON_EXE}" "${_MANPAGE_GENERATOR}"
            "${_MANPAGE_NAME}" "${CMAKE_CURRENT_SOURCE_DIR}/${markdown}" "${_MANPAGE_OUTPUT}"
    DEPENDS "${markdown}" "${_MANPAGE_GENERATOR}"
  )
endfunction()
