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
  add_library(Jemalloc::Jemalloc UNKNOWN IMPORTED)
  set_target_properties(
    Jemalloc::Jemalloc
    PROPERTIES IMPORTED_LOCATION ${Jemalloc_LIBRARIES}
               INTERFACE_INCLUDE_DIRECTORIES ${Jemalloc_INCLUDE_DIRS})
endif()
