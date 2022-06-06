# * Find Jemalloc library Find the native Jemalloc includes and library
#
# Jemalloc_INCLUDE_DIRS - where to find jemalloc.h, etc. Jemalloc_LIBRARIES -
# List of libraries when using jemalloc. Jemalloc_FOUND - True if jemalloc
# found.

find_path(Jemalloc_INCLUDE_DIRS NAMES jemalloc/jemalloc.h)
find_library(Jemalloc_LIBRARIES NAMES jemalloc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Jemalloc DEFAULT_MSG Jemalloc_LIBRARIES
                                  Jemalloc_INCLUDE_DIRS)

mark_as_advanced(Jemalloc_LIBRARIES Jemalloc_INCLUDE_DIRS)

if(Jemalloc_FOUND AND NOT (TARGET Jemalloc::Jemalloc))
#  set(CMAKE_REQUIRED_INCLUDES ${Jemalloc_INCLUDE_DIRS})
#  check_cxx_source_compiles(
#   "#include <jemalloc/jemalloc.h>
#    #if JEMALLOC_VERSION_MAJOR < 5
#    #error JEMALLOC_VERSION_MAJOR < 5
#    #endif
#    int main() {
#      return 0;
#    }"
#    JEMALLOC_VERSION_OK)
#
#  if(NOT JEMALLOC_VERSION_OK)
#    def_ext_prj_g(JEMALLOC "5.2.1")
#    message(STATUS "Collecting jemalloc - " @${JEMALLOC_TAG}  " at " ${JEMALLOC_SOURCE_DIR})
#
#    ExternalProject_Add(${JEMALLOC_PRJ}
#      PREFIX "${DEPS}"
#      GIT_REPOSITORY "https://github.com/jemalloc/jemalloc.git"
#      GIT_TAG ${JEMALLOC_TAG}
#      UPDATE_COMMAND ""
#      CMAKE_ARGS ${CMAKE_ARGUMENTS}
#      SOURCE_DIR ${JEMALLOC_SOURCE_DIR}
#      BINARY_DIR ${JEMALLOC_BINARY_DIR}
#    )
#
#  endif()
  add_library(Jemalloc::Jemalloc UNKNOWN IMPORTED)
  set_target_properties(
    Jemalloc::Jemalloc
    PROPERTIES IMPORTED_LOCATION ${Jemalloc_LIBRARIES}
               INTERFACE_INCLUDE_DIRECTORIES ${Jemalloc_INCLUDE_DIRS})

endif()
