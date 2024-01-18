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

function(add_cpp2_thrift_library idlfile)
  set(_options FROZEN METADATA CONSTANTS NO_LIBRARY)
  set(_oneValueArgs OUTPUT_PATH TARGET)
  set(_multiValueArgs)
  cmake_parse_arguments(_THRIFT "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  string(REGEX REPLACE ".*/([^/]+)\\.thrift" "\\1" _THRIFTNAME ${idlfile})

  if(NOT _THRIFT_NO_LIBRARY AND NOT _THRIFT_TARGET)
    message(FATAL_ERROR "add_cpp2_thrift_library: TARGET must be specified")
  endif()

  if(_THRIFT_FROZEN)
    list(APPEND _OPTS "frozen")
  endif()

  if(_THRIFT_METADATA)
    list(APPEND _OPTS "metadata")
  endif()

  if(_THRIFT_CONSTANTS)
    list(APPEND _OPTS "constants")
  endif()

  if(_THRIFT_NO_LIBRARY)
    list(APPEND _OPTS "nolib")
  endif()

  set(_OPTSTR "")
  if(_OPTS)
    string(JOIN ", " _OPTSTR ${_OPTS})
    set(_OPTSTR " (${_OPTSTR})")
  endif()

  list(APPEND _THRIFT_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_clients.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_data.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_data.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_for_each_field.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_handlers.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.tcc
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types_custom_protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types_fwd.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visit_by_thrift_field_metadata.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visit_union.h
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visitation.h
  )

  list(APPEND _THRIFT_CONSTANTS_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_constants.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_constants.h
  )

  list(APPEND _THRIFT_METADATA_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_metadata.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_metadata.h
  )

  set(_THRIFT_GEN mstch_cpp2)

  if(_THRIFT_FROZEN)
    list(APPEND _THRIFT_SRC
      ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_layouts.cpp
      ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_layouts.h
    )
    set(_THRIFT_GEN ${_THRIFT_GEN}:frozen2)
  endif()

  list(APPEND _THRIFT_GEN_SRC ${_THRIFT_SRC} ${_THRIFT_CONSTANTS_SRC} ${_THRIFT_METADATA_SRC})
  list(APPEND _THRIFT_LIB_SRC ${_THRIFT_SRC})

  if(_THRIFT_CONSTANTS)
    list(APPEND _THRIFT_LIB_SRC ${_THRIFT_CONSTANTS_SRC})
  endif()

  if(_THRIFT_METADATA)
    list(APPEND _THRIFT_LIB_SRC ${_THRIFT_METADATA_SRC})
  endif()

  message(STATUS "Adding thrift library [${_THRIFTNAME}] from ${idlfile}${_OPTSTR}")

  add_custom_command(
    OUTPUT thrift/${_THRIFT_OUTPUT_PATH}/_keep_${_THRIFTNAME}
    COMMAND ${CMAKE_COMMAND} -E make_directory thrift/${_THRIFT_OUTPUT_PATH}
    COMMAND ${CMAKE_COMMAND} -E touch thrift/${_THRIFT_OUTPUT_PATH}/_keep_${_THRIFTNAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  add_custom_command(
    OUTPUT ${_THRIFT_GEN_SRC}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${idlfile}
    ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/${_THRIFTNAME}.thrift
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
                -I ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
                -o ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}
                --gen ${_THRIFT_GEN} ${_THRIFTNAME}.thrift
    COMMENT "Running thrift compiler on ${_THRIFTNAME}.thrift [${_THRIFT_GEN}]"
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
            ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/_keep_${_THRIFTNAME}
            ${idlfile}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}
  )

  if(NOT _THRIFT_NO_LIBRARY)
    add_library(${_THRIFT_TARGET} ${_THRIFT_LIB_SRC})
    target_include_directories(${_THRIFT_TARGET} PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/folly
        ${CMAKE_CURRENT_BINARY_DIR}/thrift
        ${CMAKE_CURRENT_SOURCE_DIR}/folly
        ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    if(NOT WIN32)
      target_compile_options(${_THRIFT_TARGET} PRIVATE -Wno-deprecated-declarations)
    endif()
    set_property(TARGET ${_THRIFT_TARGET} PROPERTY CXX_STANDARD 20)
  endif()
endfunction()
