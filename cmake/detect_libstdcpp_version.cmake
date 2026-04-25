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

include_guard(GLOBAL)

function(detect_libstdcpp_version out_var)
  set(${out_var} NOTFOUND PARENT_SCOPE)

  set(_src "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/detect_libstdcpp_version.cpp")
  file(WRITE "${_src}" "#include <bits/c++config.h>\n")

  set(_cmd ${CMAKE_CXX_COMPILER})

  separate_arguments(_cxx_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")

  execute_process(
    COMMAND ${_cmd} ${_cxx_flags} -dM -E -x c++ "${_src}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _macros
    ERROR_QUIET
  )

  if(_rc EQUAL 0)
    string(REGEX MATCH "#define _GLIBCXX_RELEASE[ \t]+([0-9]+)" _match "${_macros}")
    if(_match)
      set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
  endif()
endfunction()
