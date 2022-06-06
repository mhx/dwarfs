# Copyright (c) 2021-2022 [Ribose Inc](https://www.ribose.com).
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


# ...................................................................
# DEF_EXT_PRJ_T
# This function defines version number and archieve hash for
# external project that is build from tarball
# For external project XXX this function will set the following variables:
# XXX_VER                -- package version
# XXX_HASH               -- tarball hash
# Version and hash value come from environment variables (GHA case)
# If environment variables are not set then default values are used
# XXX_NAME = xxx  (lowercase)
# XXX_PRJ  = _xxx (lowercase)             -- CMake ExternalProject name
# XXX_SOURCE_DIR = ${DEPS}/src/_xxx       -- Source folder for ExternalProject_Add
# XXX_BINARY_DIR = ${DEPS}/src/_xxx-build -- Build folder for ExternalProject_Add

function(DEF_EXT_PRJ_T NAME DEFAULT_VER DEFAULT_HASH)
  set(VER ${NAME}_VER)
  if (DEFINED ENV{${VER}})
    set(${VER} $ENV{${VER}} PARENT_SCOPE)
  else ()
    set(${VER} ${DEFAULT_VER} PARENT_SCOPE)
  endif()

  set(HASH ${NAME}_HASH)
  if (DEFINED ENV{${HASH})
    set(${HASH} $ENV{${HASH}} PARENT_SCOPE)
  else()
    set(${HASH} ${DEFAULT_HASH} PARENT_SCOPE)
  endif()

  set(R_NAME ${NAME}_NAME)
  string(TOLOWER ${NAME} TMP)
  set(${R_NAME} ${TMP} PARENT_SCOPE)
  set(R_PRJ  ${NAME}_PRJ)
  set(${R_PRJ} _${TMP} PARENT_SCOPE)
  set(R_DIR ${NAME}_SOURCE_DIR)
  set(${R_DIR} ${DEPS}/src/_${TMP} PARENT_SCOPE)
  set(R_DIR ${NAME}_BINARY_DIR)
  set(${R_DIR} ${DEPS}/src/_${TMP}-build PARENT_SCOPE)
endfunction()

# ...................................................................
# DEF_EXT_PRJ_G
# This function defines git tag for external project that is build from git
# For external project XXX this function will set the following variables:
# XXX_TAG                -- git tag
# Version and tag come from environment variables (GHA case)
# If environment variables are not set then default values are used
# XXX_NAME = xxx  (lowercase)
# XXX_PRJ  = _xxx (lowercase)             -- CMake ExternalProject name
# XXX_SOURCE_DIR = ${DEPS}/src/_xxx       -- Source folder for ExternalProject_Add
# XXX_BINARY_DIR = ${DEPS}/src/_xxx-build -- Build folder for ExternalProject_Add

function(DEF_EXT_PRJ_G NAME DEFAULT_TAG)
  set(TAG ${NAME}_TAG)
  if (DEFINED ENV{${TAG})
    set(${TAG} $ENV{${TAG}} PARENT_SCOPE)
  else()
    set(${TAG} ${DEFAULT_TAG} PARENT_SCOPE)
  endif()

  set(R_NAME ${NAME}_NAME)
  string(TOLOWER ${NAME} TMP)
  set(${R_NAME} ${TMP} PARENT_SCOPE)
  set(R_PRJ  ${NAME}_PRJ)
  set(${R_PRJ} _${TMP} PARENT_SCOPE)
  set(R_DIR ${NAME}_SOURCE_DIR)
  set(${R_DIR} ${DEPS}/src/_${TMP} PARENT_SCOPE)
  set(R_DIR ${NAME}_BINARY_DIR)
  set(${R_DIR} ${DEPS}/src/_${TMP}-build PARENT_SCOPE)
endfunction()