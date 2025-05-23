cmake_minimum_required(VERSION 3.5)

project(fsst)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_VERBOSE_MAKEFILE ON)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

if(NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O3 /DNDEBUG")
endif()

if(CMAKE_BUILD_TYPE STREQUAL Release)
set_source_files_properties( 
    fsst_avx512.cpp 
    PROPERTIES
    COMPILE_FLAGS -O1
)
endif()

add_library(fsst libfsst.cpp fsst_avx512.cpp fsst_avx512_unroll1.inc fsst_avx512_unroll2.inc fsst_avx512_unroll3.inc fsst_avx512_unroll4.inc)
add_executable(binary fsst.cpp)
target_link_libraries (binary LINK_PUBLIC fsst)
target_link_libraries (binary LINK_PUBLIC Threads::Threads)
set_target_properties(binary PROPERTIES OUTPUT_NAME fsst)

add_library(fsst12 libfsst12.cpp)
add_executable(binary12 fsst.cpp)
target_link_libraries (binary12 LINK_PUBLIC fsst12)
target_link_libraries (binary12 LINK_PUBLIC Threads::Threads)
set_target_properties(binary12 PROPERTIES OUTPUT_NAME fsst12)
set_target_properties(binary12 PROPERTIES COMPILE_FLAGS -DFSST12)
