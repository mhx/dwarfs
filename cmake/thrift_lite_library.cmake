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
    find_program(_PYTHON_EXE NAMES python3 python)
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
  endif()
endfunction()
