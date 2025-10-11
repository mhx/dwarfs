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

  if(DWARFS_GIT_BUILD)
    set(_THRIFT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(_THRIFT_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  list(APPEND _THRIFT_SRC
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_clients.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_data.cpp
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_data.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_for_each_field.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_handlers.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.cpp
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types_compact.cpp
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types.tcc
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types_custom_protocol.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_types_fwd.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visit_by_thrift_field_metadata.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visit_union.h
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_visitation.h
  )

  list(APPEND _THRIFT_CONSTANTS_SRC
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_constants.cpp
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_constants.h
  )

  list(APPEND _THRIFT_METADATA_SRC
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_metadata.cpp
    ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_metadata.h
  )

  set(_THRIFT_GEN mstch_cpp2)

  if(_THRIFT_FROZEN)
    list(APPEND _THRIFT_SRC
      ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_layouts.cpp
      ${_THRIFT_GENERATED_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/gen-cpp2/${_THRIFTNAME}_layouts.h
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

  if(DWARFS_GIT_BUILD)
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
      COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0 --
                  ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
                  -I ${CMAKE_CURRENT_SOURCE_DIR}/fbthrift
                  -o ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}
                  --gen ${_THRIFT_GEN} ${_THRIFTNAME}.thrift
      COMMENT "Running thrift compiler on ${_THRIFTNAME}.thrift [${_THRIFT_GEN}]"
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bin/thrift1
              ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}/_keep_${_THRIFTNAME}
              ${idlfile}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/thrift/${_THRIFT_OUTPUT_PATH}
    )
  endif()

  if(NOT _THRIFT_NO_LIBRARY)
    add_library(${_THRIFT_TARGET} OBJECT ${_THRIFT_LIB_SRC})
    target_include_directories(${_THRIFT_TARGET} PUBLIC
      $<BUILD_INTERFACE:${_THRIFT_GENERATED_DIR}/thrift>
    )
    target_link_libraries(${_THRIFT_TARGET} PUBLIC dwarfs_thrift_lite)
    if(NOT WIN32)
      target_compile_options(${_THRIFT_TARGET} PRIVATE -Wno-deprecated-declarations)
    endif()
    set_property(TARGET ${_THRIFT_TARGET} PROPERTY CXX_STANDARD ${DWARFS_CXX_STANDARD})
  endif()
endfunction()
