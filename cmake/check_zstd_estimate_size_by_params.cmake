include_guard(GLOBAL)

function(check_zstd_estimate_size_by_cctx_params zstd_target out_var)
  set(${out_var} OFF PARENT_SCOPE)

  if(TARGET "${zstd_target}")
    get_target_property(_zstd_target_link_libraries "${zstd_target}" INTERFACE_LINK_LIBRARIES)
    message(STATUS "check_zstd_estimate_size_by_cctx_params: checking '${zstd_target}' (${_zstd_target_link_libraries})")
  else()
    message(FATAL_ERROR "check_zstd_estimate_size_by_cctx_params: '${zstd_target}' is not a target")
  endif()

  try_run(
    _run_result
    _compile_result
    "${CMAKE_BINARY_DIR}/CMakeFiles/zstd_estimate_size_check"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/zstd_estimate_size_check.c"
    LINK_LIBRARIES "${zstd_target}"
    COMPILE_OUTPUT_VARIABLE _compile_out
    RUN_OUTPUT_VARIABLE     _run_out
  )

  if(DEFINED ZSTD_ESTIMATE_SIZE_CHECK_DEBUG AND ZSTD_ESTIMATE_SIZE_CHECK_DEBUG)
    message(STATUS "compile_result=${_compile_result}")
    message(STATUS "run_result=${_run_result}")
    message(STATUS "compile output:\n${_compile_out}")
    message(STATUS "run output:\n${_run_out}")
  endif()

  if(_compile_result AND _run_result EQUAL 0)
    message(STATUS "check_zstd_estimate_size_by_cctx_params: '${zstd_target}' supports estimate size by cctx params")
    set(${out_var} ON PARENT_SCOPE)
  else()
    message(STATUS "check_zstd_estimate_size_by_cctx_params: '${zstd_target}' does NOT support estimate size by cctx params")
  endif()
endfunction()
