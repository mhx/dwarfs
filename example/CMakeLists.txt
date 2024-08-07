cmake_minimum_required(VERSION 3.28.0)

project(dwarfs-example)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

find_package(dwarfs REQUIRED CONFIG)

add_executable(example example.cpp)
target_link_libraries(example dwarfs::dwarfs_reader dwarfs::dwarfs_extractor)
