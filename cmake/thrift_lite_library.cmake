# vim:set ts=2 sw=2 sts=2 et:
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

function(add_thrift_lite_library idlfile)
  set(_options FROZEN NO_LIBRARY)
  set(_oneValueArgs OUTPUT_PATH TARGET)
  set(_multiValueArgs)
  cmake_parse_arguments(_THRIFT "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  string(REGEX REPLACE ".*/([^/]+)\\.thrift" "\\1" _THRIFTNAME ${idlfile})

  if(NOT _THRIFT_NO_LIBRARY AND NOT _THRIFT_TARGET)
    message(FATAL_ERROR "add_thrift_lite_library: TARGET must be specified")
  endif()

  if(DWARFS_GIT_BUILD)
    set(_THRIFT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(_THRIFT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  list(APPEND _THRIFT_SRC
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite/${_THRIFTNAME}_types.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite/${_THRIFTNAME}_types-inl.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite/${_THRIFTNAME}_types.cpp
  )

  if(_THRIFT_FROZEN)
    list(APPEND _THRIFT_SRC
      ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite/${_THRIFTNAME}_layouts.h
      ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite/${_THRIFTNAME}_layouts.cpp
    )
  endif()

  message(STATUS "Adding thrift lite library [${_THRIFTNAME}] from ${idlfile}")

  if(DWARFS_GIT_BUILD)
    find_program(_PYTHON_EXE python3 python)
    if(NOT _PYTHON_EXE)
      find_package(Python3 REQUIRED)
      set(_PYTHON_EXE "${Python3_EXECUTABLE}")
    endif()

    if(_THRIFT_FROZEN)
      list(APPEND _TL_OPTS "--frozen")
    endif()

    add_custom_command(
      OUTPUT ${_THRIFT_SRC}
      COMMAND "${_PYTHON_EXE}" ${CMAKE_CURRENT_SOURCE_DIR}/cmake/thrift-lite.py
                  --input ${CMAKE_CURRENT_SOURCE_DIR}/${idlfile}
                  --output-dir ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp-lite
                  ${_TL_OPTS}
      COMMENT "Running thrift lite compiler on ${_THRIFTNAME}.xml [cpp]"
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/thrift-lite.py
              ${CMAKE_CURRENT_SOURCE_DIR}/${idlfile}
    )
  endif()

  if(NOT _THRIFT_NO_LIBRARY)
    add_library(${_THRIFT_TARGET} OBJECT ${_THRIFT_SRC})
    target_include_directories(${_THRIFT_TARGET} PUBLIC
      $<BUILD_INTERFACE:${_THRIFT_GENERATED_DIR}/thrift>
    )
    target_link_libraries(${_THRIFT_TARGET} PUBLIC dwarfs_thrift_lite_v2)
    set_property(TARGET ${_THRIFT_TARGET} PROPERTY CXX_STANDARD ${DWARFS_CXX_STANDARD})
  endif()
endfunction()
