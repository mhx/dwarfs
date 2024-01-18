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

cmake_minimum_required(VERSION 3.25.0)

function(add_manpage_source markdown)
  set(_options)
  set(_oneValueArgs NAME OUTPUT)
  set(_multiValueArgs)
  cmake_parse_arguments(_MANPAGE "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  find_program(_PYTHON_EXE python python3)
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
