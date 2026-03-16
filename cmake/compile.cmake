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

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(BUILD_SHARED_LIBS AND STATIC_BUILD_DO_NOT_USE)
  message(FATAL_ERROR "Seriously, don't try setting both BUILD_SHARED_LIBS and STATIC_BUILD_DO_NOT_USE")
endif()

if(WIN32 OR STATIC_BUILD_DO_NOT_USE)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
else()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT DISABLE_CCACHE)
  find_program(CCACHE_EXE NAMES ccache ccache.exe PATHS "c:/bin")
  if(CCACHE_EXE)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXE})
    if(DEFINED ENV{GITHUB_REF_TYPE})
      message(STATUS "Using ccache from ${CCACHE_EXE}")
      execute_process(COMMAND ${CCACHE_EXE} -p OUTPUT_VARIABLE CCACHE_CONFIG ERROR_QUIET)
      message(STATUS "ccache config:\n${CCACHE_CONFIG}")
    endif()
  endif()
endif()

if(DEFINED ENV{CCACHE_PREFIX})
  add_compile_options(-Wno-gnu-line-marker)
endif()

if(NOT WIN32)
  if(NOT DISABLE_MOLD)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      find_program(MOLD_EXE NAMES mold)
      if(MOLD_EXE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${MOLD_EXE}")
      endif()
    endif()
  endif()

  find_program(LDD_EXE NAMES ldd)
  if(LDD_EXE)
    execute_process(COMMAND ${LDD_EXE} --version ERROR_VARIABLE LDD_VERSION)
    if(LDD_VERSION MATCHES "musl libc")
      add_compile_definitions(_LARGEFILE64_SOURCE)
    endif()
  endif()

  include(CheckFunctionExists)
  check_function_exists(close_range HAVE_CLOSE_RANGE)
  if (HAVE_CLOSE_RANGE)
    add_compile_definitions(DWARFS_HAVE_CLOSE_RANGE=1)
  endif()
endif()

set(default_build_type "Release")

set(CMAKE_CXX_STANDARD ${DWARFS_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}'")
  set(CMAKE_BUILD_TYPE
      "${default_build_type}"
      CACHE STRING "Build Type" FORCE)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-fdiagnostics-color=always)
  # For gcc, -O3 is *much* worse than -O2
  # Update: This is still true for gcc-12
  # set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -g")
  # set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -g")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_compile_options(-fcolor-diagnostics)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/Zc:__cplusplus /utf-8 /wd4267 /wd4244 /wd5219)

  # Apply /MT or /MTd  (multithread, static version of the run-time library)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:Embedded>")
  set(MSVC_USE_STATIC_RUNTIME ON CACHE BOOL "static build")

  add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)

  foreach(CompilerFlag CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REPLACE "/RTC1" "" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()
endif()

if(ENABLE_STACKTRACE)
  if(CMAKE_BUILD_TYPE STREQUAL Release)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
       "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
      # enable line numbers in stack traces
      add_compile_options(-g1)
    endif()
  endif()
endif()

if(DWARFS_OPTIMIZE)
  string(REPLACE "-O3" "-O${DWARFS_OPTIMIZE}" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REPLACE "-O3" "-O${DWARFS_OPTIMIZE}" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

if(APPLE)
  include(CheckLinkerFlag)
  check_linker_flag(CXX "-Wl,-no_warn_duplicate_libraries" APPLE_LD_NO_WARN_DUPLICATE_LIBRARIES)
  if(APPLE_LD_NO_WARN_DUPLICATE_LIBRARIES)
    add_link_options("-Wl,-no_warn_duplicate_libraries")
  endif()
endif()

function(brand_elf_linux target)
  if(STATIC_BUILD_DO_NOT_USE)
    if(NOT CMAKE_ELFEDIT)
      if(DEFINED ENV{ELFEDIT_TOOL})
        set(CMAKE_ELFEDIT $ENV{ELFEDIT_TOOL})
      endif()
    endif()
    if(CMAKE_ELFEDIT)
      add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_ELFEDIT}" --output-osabi=Linux "$<TARGET_FILE:${target}>"
        VERBATIM
      )
    endif()
  endif()
endfunction()
