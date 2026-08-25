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

function(add_manpage MANPAGE)
  if(DWARFS_GIT_BUILD)
    find_program(_PYTHON_EXE NAMES python3 python)
    if(NOT _PYTHON_EXE)
      find_package(Python3 REQUIRED)
      set(_PYTHON_EXE "${Python3_EXECUTABLE}")
    endif()

    find_program(_MANDOC_EXE NAMES mandoc)
  endif()

  if(NOT TARGET manpages)
    add_custom_target(manpages ALL)
    set_target_properties(manpages PROPERTIES MANPAGE_DIRECTORIES "")
  endif()

  string(REGEX MATCH "^[^.]*" _docname "${MANPAGE}")
  string(REGEX MATCH "[^.]*$" _section "${MANPAGE}")

  set(_man_dir "")

  if(DWARFS_GIT_BUILD)
    set(_man_generator "${CMAKE_SOURCE_DIR}/cmake/render_manpage_v2.py")
    set(_man_input "${CMAKE_CURRENT_SOURCE_DIR}/doc/${_docname}.md")
    set(_man_dir "${CMAKE_CURRENT_BINARY_DIR}/man${_section}")
    set(_man_output "${_man_dir}/${MANPAGE}")
    set(_mandoc_lint)
    if(_MANDOC_EXE)
      message(STATUS "mandoc linting enabled for ${MANPAGE}")
      list(APPEND _mandoc_lint COMMAND "${_MANDOC_EXE}" -T lint -W all "${_man_output}")
    else()
      message(WARNING "mandoc not found, skipping manpage linting for ${MANPAGE}")
    endif()
    add_custom_command(
      OUTPUT "${_man_output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_man_dir}"
      COMMAND "${_PYTHON_EXE}" "${_man_generator}" --strict --man "${_man_input}" "${_man_output}"
      ${_mandoc_lint}
      DEPENDS "${_man_generator}" "${_man_input}")
    add_custom_target("_manpage_${_docname}_${_section}" DEPENDS "${_man_output}")
    add_dependencies(manpages "_manpage_${_docname}_${_section}")
  else()
    set(_man_dir "${CMAKE_CURRENT_SOURCE_DIR}/doc/man${_section}")
  endif()

  if(_man_dir)
    install(FILES "${_man_dir}/${MANPAGE}" DESTINATION share/man/man${_section})
    get_target_property(_man_dirs manpages MANPAGE_DIRECTORIES)
    list(FIND _man_dirs "${_man_dir}" _index)
    if(${_index} EQUAL -1)
      list(APPEND _man_dirs "${_man_dir}")
      set_target_properties(manpages PROPERTIES MANPAGE_DIRECTORIES "${_man_dirs}")
    endif()
  endif()
endfunction()
