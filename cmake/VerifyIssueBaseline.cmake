if(NOT DEFINED HSC OR HSC STREQUAL "")
  message(FATAL_ERROR "HSC is required")
endif()
if(NOT DEFINED CASE OR CASE STREQUAL "")
  message(FATAL_ERROR "CASE is required")
endif()
if(NOT DEFINED EXPECT_FILE OR NOT EXISTS "${EXPECT_FILE}")
  message(FATAL_ERROR "EXPECT_FILE is required and must exist")
endif()
if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

include("${EXPECT_FILE}")

if(NOT DEFINED CASE_ACTION)
  message(FATAL_ERROR "CASE_ACTION is required in ${EXPECT_FILE}")
endif()
if(NOT DEFINED EXPECT_COMPILE_EXIT)
  message(FATAL_ERROR "EXPECT_COMPILE_EXIT is required in ${EXPECT_FILE}")
endif()

function(assert_matches label value pattern)
  if(NOT "${value}" MATCHES "${pattern}")
    message(FATAL_ERROR "${label} did not match `${pattern}`. Actual:\n${value}")
  endif()
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(output_path "${OUTPUT_DIR}/program")
set(llvm_path "${OUTPUT_DIR}/module.ll")

if(CASE_ACTION STREQUAL "run" OR CASE_ACTION STREQUAL "compile")
  execute_process(
    COMMAND "${HSC}" ${CASE_FLAGS} "${CASE}" -o "${output_path}"
    RESULT_VARIABLE compile_exit
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
  )
elseif(CASE_ACTION STREQUAL "emit-llvm")
  execute_process(
    COMMAND "${HSC}" ${CASE_FLAGS} "${CASE}" -o "${llvm_path}"
    RESULT_VARIABLE compile_exit
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
  )
else()
  message(FATAL_ERROR "unsupported CASE_ACTION `${CASE_ACTION}`")
endif()

if(NOT "${compile_exit}" STREQUAL "${EXPECT_COMPILE_EXIT}")
  message(FATAL_ERROR
    "compile exit mismatch: expected ${EXPECT_COMPILE_EXIT}, got ${compile_exit}.\n${compile_stderr}")
endif()

if(DEFINED EXPECT_COMPILE_STDOUT_REGEX)
  assert_matches("compile stdout" "${compile_stdout}" "${EXPECT_COMPILE_STDOUT_REGEX}")
endif()
if(DEFINED EXPECT_COMPILE_STDERR_REGEX)
  assert_matches("compile stderr" "${compile_stderr}" "${EXPECT_COMPILE_STDERR_REGEX}")
endif()

if(CASE_ACTION STREQUAL "run" AND "${compile_exit}" STREQUAL "0")
  execute_process(
    COMMAND "${output_path}"
    RESULT_VARIABLE run_exit
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
  )
  if(NOT "${run_exit}" STREQUAL "${EXPECT_RUN_EXIT}")
    message(FATAL_ERROR
      "run exit mismatch: expected ${EXPECT_RUN_EXIT}, got ${run_exit}.\n${run_stderr}")
  endif()
  if(DEFINED EXPECT_RUN_STDOUT_REGEX)
    assert_matches("run stdout" "${run_stdout}" "${EXPECT_RUN_STDOUT_REGEX}")
  endif()
  if(DEFINED EXPECT_RUN_STDERR_REGEX)
    assert_matches("run stderr" "${run_stderr}" "${EXPECT_RUN_STDERR_REGEX}")
  endif()
endif()

if(CASE_ACTION STREQUAL "emit-llvm" AND "${compile_exit}" STREQUAL "0")
  if(NOT EXISTS "${llvm_path}")
    message(FATAL_ERROR "expected LLVM IR at ${llvm_path}")
  endif()
  file(READ "${llvm_path}" llvm_ir)
  if(DEFINED EXPECT_IR_REGEX)
    assert_matches("LLVM IR" "${llvm_ir}" "${EXPECT_IR_REGEX}")
  endif()
  if(DEFINED FORBID_IR_REGEX AND "${llvm_ir}" MATCHES "${FORBID_IR_REGEX}")
    message(FATAL_ERROR "LLVM IR unexpectedly matched `${FORBID_IR_REGEX}`")
  endif()
endif()
