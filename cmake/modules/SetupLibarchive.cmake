# Copyright (c) 2022 [Ribose Inc](https://www.ribose.com).
# All rights reserved.
# This file is a part of tebako
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

def_ext_prj_t(LIBARCHIVE "3.5.2" "f0b19ff39c3c9a5898a219497ababbadab99d8178acc980155c7e1271089b5a0")

message(STATUS "Collecting libarchive - " v${LIBARCHIVE_VER} " at " ${LIBARCHIVE_SOURCE_DIR})

set(CMAKE_ARGUMENTS -DCMAKE_INSTALL_PREFIX=${DEPS}
                    -DCMAKE_BUILD_TYPE=Release
                    -DENABLE_ACL:BOOL=OFF
                    -DENABLE_ICONV:BOOL=OFF
                    -DENABLE_LIBXML2:BOOL=OFF
                    -DENABLE_TEST:BOOL=OFF
                    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
                    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
)

if(TEBAKO_BUILD_TARGET)
  list(APPEND CMAKE_ARGUMENTS  -DCMAKE_C_FLAGS=--target=${TEBAKO_BUILD_TARGET})
  list(APPEND CMAKE_ARGUMENTS  -DCMAKE_EXE_LINKER_FLAGS=--target=${TEBAKO_BUILD_TARGET})
  list(APPEND CMAKE_ARGUMENTS  -DCMAKE_SHARED_LINKER_FLAGS=--target=${TEBAKO_BUILD_TARGET})
endif(TEBAKO_BUILD_TARGET)

set(__LIBARCHIVE "${DEPS}/lib/libarchive.a")

ExternalProject_Add(${LIBARCHIVE_PRJ}
  PREFIX "${DEPS}"
  URL http://www.libarchive.org/downloads/libarchive-${LIBARCHIVE_VER}.tar.xz
  URL_HASH SHA256=${LIBARCHIVE_HASH}
  DOWNLOAD_NO_PROGRESS true
  UPDATE_COMMAND ""
  CMAKE_ARGS ${CMAKE_ARGUMENTS}
  SOURCE_DIR ${LIBARCHIVE_SOURCE_DIR}
  BINARY_DIR ${LIBARCHIVE_BINARY_DIR}
)

add_library(_LIBARCHIVE STATIC IMPORTED)
set_target_properties(_LIBARCHIVE PROPERTIES IMPORTED_LOCATION  ${__LIBARCHIVE})
add_dependencies(_LIBARCHIVE ${LIBARCHIVE_PRJ})
