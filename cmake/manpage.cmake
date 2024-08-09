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

function(add_manpage MANPAGE)
  if(DWARFS_GIT_BUILD)
    find_program(RONN_EXE ronn DOC "ronn man page generator" REQUIRED)
  endif()

  if(NOT TARGET manpages)
    add_custom_target(manpages ALL)
    set_target_properties(manpages PROPERTIES MANPAGE_DIRECTORIES "")
  endif()

  string(REGEX MATCH "^[^.]*" _docname "${MANPAGE}")
  string(REGEX MATCH "[^.]*$" _section "${MANPAGE}")

  set(_man_dir "")

  if(DWARFS_GIT_BUILD)
    set(_man_input "${CMAKE_CURRENT_SOURCE_DIR}/doc/${_docname}.md")

    execute_process(
      COMMAND ${RONN_EXE}
      INPUT_FILE "${_man_input}"
      RESULT_VARIABLE _ronn_result
      OUTPUT_VARIABLE _ronn_output
      ERROR_VARIABLE _ronn_error)

    if(${_ronn_result} EQUAL 0)
      set(_man_dir "${CMAKE_CURRENT_BINARY_DIR}/man${_section}")
      set(_man_output "${_man_dir}/${MANPAGE}")
      add_custom_command(
        OUTPUT "${_man_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_man_dir}"
        COMMAND ${RONN_EXE} <"${_man_input}" >"${_man_output}"
        DEPENDS "${_man_input}")
      add_custom_target("_manpage_${_docname}_${_section}" DEPENDS "${_man_output}")
      add_dependencies(manpages "_manpage_${_docname}_${_section}")
    else()
      message(WARNING "${RONN_EXE} failed to process ${_man_input} -> ${MANPAGE}")
      message(WARNING "error: ${_ronn_error}")
    endif()
  else()
    set(_man_dir "${CMAKE_CURRENT_SOURCE_DIR}/doc/man${_section}")
  endif()

  if(_man_dir)
    get_target_property(_man_dirs manpages MANPAGE_DIRECTORIES)
    list(FIND _man_dirs "${_man_dir}" _index)
    if(${_index} EQUAL -1)
      list(APPEND _man_dirs "${_man_dir}")
      install(DIRECTORY "${_man_dir}" DESTINATION share/man)
      set_target_properties(manpages PROPERTIES MANPAGE_DIRECTORIES "${_man_dirs}")
    endif()
  endif()
endfunction()
