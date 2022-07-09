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

def_ext_prj_g(FBTHRIFT "45bea52")

message(STATUS "Collecting fbthrift - " @${FBTHRIFT_TAG}  " at " ${FBTHRIFT_SOURCE_DIR})

set(CMAKE_ARGUMENTS -DCMAKE_INSTALL_PREFIX=${DEPS}
                    -DCMAKE_BUILD_TYPE=Release
                    -Dcompiler_only:BOOL=ON
)
if(BUILD_CMAKE_ARGUMENTS)
  list(APPEND CMAKE_ARGUMENTS ${BUILD_CMAKE_ARGUMENTS})
endif()

ExternalProject_Add(${FBTHRIFT_PRJ}
  PREFIX "${DEPS}"
  GIT_REPOSITORY "https://github.com/facebook/fbthrift.git"
  GIT_TAG ${FBTHRIFT_TAG}
  UPDATE_COMMAND ""
  PATCH_COMMAND "${GNU_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci-scripts/patch-fbthrift.sh" "${FBTHRIFT_SOURCE_DIR}"
  CMAKE_ARGS ${CMAKE_ARGUMENTS}
  SOURCE_DIR ${FBTHRIFT_SOURCE_DIR}
  BINARY_DIR ${FBTHRIFT_BINARY_DIR}
)
