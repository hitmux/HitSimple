function(require_success result output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}")
  endif()
endfunction()

foreach(variable HSC CLANG LLVM_PROFDATA SOURCE MISMATCH_SOURCE WORK_DIR)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "missing required ${variable} test argument")
  endif()
endforeach()

set(plain_program "${WORK_DIR}/plain-program")
set(instrumented_program "${WORK_DIR}/instrumented-program")
set(profile_raw "${WORK_DIR}/program.profraw")
set(profile_data "${WORK_DIR}/program.profdata")
set(profile_report "${WORK_DIR}/profile.txt")
set(pgo_program "${WORK_DIR}/pgo-program")
set(instrumented_stdout "${WORK_DIR}/instrumented.stdout")
set(pgo_stdout "${WORK_DIR}/pgo.stdout")
set(mismatch_program "${WORK_DIR}/mismatch-program")
set(clang_wrapper "${WORK_DIR}/clang-wrapper")
set(clang_argument_log "${WORK_DIR}/clang.args")

file(REMOVE "${clang_argument_log}")

execute_process(
  COMMAND "${HSC}" -O2 "${SOURCE}" -o "${plain_program}"
  RESULT_VARIABLE plain_compile_result
  OUTPUT_VARIABLE plain_compile_output
  ERROR_VARIABLE plain_compile_error)
require_success("${plain_compile_result}"
                "${plain_compile_output}${plain_compile_error}")

file(WRITE "${clang_wrapper}" [=[#!/bin/sh
printf '%s\n' "$@" >> "$HITSIMPLE_CLANG_ARGUMENT_LOG"
exec "$HITSIMPLE_REAL_CLANG" "$@"
]=])
file(CHMOD "${clang_wrapper}"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
              WORLD_READ WORLD_EXECUTE)

set(native_environment
    "HITSIMPLE_OPT=${WORK_DIR}/missing-opt"
    "HITSIMPLE_CLANG=${clang_wrapper}"
    "HITSIMPLE_REAL_CLANG=${CLANG}"
    "HITSIMPLE_CLANG_ARGUMENT_LOG=${clang_argument_log}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${native_environment}
          "${HSC}" -O2 "--pgo-instrument=${profile_raw}" "${SOURCE}"
          -o "${instrumented_program}"
  RESULT_VARIABLE instrument_compile_result
  OUTPUT_VARIABLE instrument_compile_output
  ERROR_VARIABLE instrument_compile_error)
require_success("${instrument_compile_result}"
                "${instrument_compile_output}${instrument_compile_error}")

execute_process(
  COMMAND "${instrumented_program}"
  RESULT_VARIABLE instrument_run_result
  OUTPUT_FILE "${instrumented_stdout}"
  ERROR_VARIABLE instrument_run_error)
require_success("${instrument_run_result}" "${instrument_run_error}")
if(NOT EXISTS "${profile_raw}" OR IS_DIRECTORY "${profile_raw}")
  message(FATAL_ERROR "instrumented program did not create ${profile_raw}")
endif()

execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse "${profile_raw}" -o "${profile_data}"
  RESULT_VARIABLE profile_merge_result
  OUTPUT_VARIABLE profile_merge_output
  ERROR_VARIABLE profile_merge_error)
require_success("${profile_merge_result}"
                "${profile_merge_output}${profile_merge_error}")

execute_process(
  COMMAND "${LLVM_PROFDATA}" show --all-functions --counts "${profile_data}"
  RESULT_VARIABLE profile_show_result
  OUTPUT_VARIABLE profile_show_output
  ERROR_VARIABLE profile_show_error)
require_success("${profile_show_result}"
                "${profile_show_output}${profile_show_error}")
file(WRITE "${profile_report}" "${profile_show_output}")
if(NOT "${profile_show_output}" MATCHES "hot:" OR
   NOT "${profile_show_output}" MATCHES "Block counts:")
  message(FATAL_ERROR "merged profile does not contain the hot function:\n${profile_show_output}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${native_environment}
          "${HSC}" -O2 "--pgo-use=${profile_data}" "${SOURCE}"
          -o "${pgo_program}"
  RESULT_VARIABLE use_compile_result
  OUTPUT_VARIABLE use_compile_output
  ERROR_VARIABLE use_compile_error)
require_success("${use_compile_result}"
                "${use_compile_output}${use_compile_error}")

execute_process(
  COMMAND "${pgo_program}"
  RESULT_VARIABLE use_run_result
  OUTPUT_FILE "${pgo_stdout}"
  ERROR_VARIABLE use_run_error)
require_success("${use_run_result}" "${use_run_error}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${instrumented_stdout}"
          "${pgo_stdout}"
  RESULT_VARIABLE output_compare_result)
if(NOT output_compare_result EQUAL 0)
  message(FATAL_ERROR "instrumented and PGO-use programs produced different output")
endif()

file(SHA256 "${plain_program}" plain_hash)
file(SHA256 "${pgo_program}" pgo_hash)
if("${plain_hash}" STREQUAL "${pgo_hash}")
  message(FATAL_ERROR "PGO-use did not produce an observable native code change")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${native_environment}
          "${HSC}" -O2 "--pgo-use=${profile_data}" "${MISMATCH_SOURCE}"
          -o "${mismatch_program}"
  RESULT_VARIABLE mismatch_compile_result
  OUTPUT_VARIABLE mismatch_compile_output
  ERROR_VARIABLE mismatch_compile_error)
require_success("${mismatch_compile_result}"
                "${mismatch_compile_output}${mismatch_compile_error}")
if(NOT "${mismatch_compile_error}" MATCHES "hash mismatch")
  message(FATAL_ERROR "PGO profile mismatch did not report a hash mismatch:\n${mismatch_compile_output}${mismatch_compile_error}")
endif()

file(READ "${clang_argument_log}" clang_arguments)
set(clang_arguments_with_newlines "\n${clang_arguments}\n")
if(NOT clang_arguments MATCHES "hitsimple-0\\.o")
  message(FATAL_ERROR "PGO link did not receive the native HitSimple object:\n${clang_arguments}")
endif()
if(clang_arguments_with_newlines MATCHES "\n-x\nir\n" OR
   clang_arguments MATCHES "\\.ll($|\n)" OR
   clang_arguments_with_newlines MATCHES "\n-c\n")
  message(FATAL_ERROR "PGO link received LLVM IR instead of only native objects:\n${clang_arguments}")
endif()
