if(NOT DEFINED HSC OR HSC STREQUAL "")
  message(FATAL_ERROR "HSC is required")
endif()
if(NOT DEFINED CLANG OR CLANG STREQUAL "")
  message(FATAL_ERROR "CLANG is required")
endif()
if(NOT DEFINED RUNTIME OR RUNTIME STREQUAL "")
  message(FATAL_ERROR "RUNTIME is required")
endif()
if(NOT DEFINED SOURCE OR SOURCE STREQUAL "")
  message(FATAL_ERROR "SOURCE is required")
endif()

execute_process(
  COMMAND uname -m
  OUTPUT_VARIABLE machine
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE machine_result
)
if(NOT machine_result EQUAL 0 OR NOT machine MATCHES "^(aarch64|arm64)$")
  message(FATAL_ERROR
    "strict-alignment execution requires a native AArch64 runner, got '${machine}'")
endif()

set(llvm_ir "${CMAKE_CURRENT_BINARY_DIR}/strict-alignment.ll")
set(program "${CMAKE_CURRENT_BINARY_DIR}/strict-alignment")

execute_process(
  COMMAND "${HSC}" --emit-llvm "${SOURCE}" -o "${llvm_ir}"
  RESULT_VARIABLE emit_result
  OUTPUT_VARIABLE emit_stdout
  ERROR_VARIABLE emit_stderr
)
if(NOT emit_result EQUAL 0)
  message(FATAL_ERROR
    "hsc failed to emit the strict-alignment fixture (exit ${emit_result})\n"
    "stdout:\n${emit_stdout}\nstderr:\n${emit_stderr}")
endif()

file(READ "${llvm_ir}" llvm_text)
string(REGEX MATCH "store i64 [^\n]*align 1" packed_store "${llvm_text}")
string(REGEX MATCH "load i64, ptr [^\n]*align 1" packed_load "${llvm_text}")
if(packed_store STREQUAL "" OR packed_load STREQUAL "")
  message(FATAL_ERROR
    "packed u64 access must retain align 1 before strict-alignment execution")
endif()

execute_process(
  COMMAND "${CLANG}" -O2 -mstrict-align "${llvm_ir}" "${RUNTIME}" -lm
    -o "${program}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_stdout
  ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR
    "Clang failed to compile the strict-alignment fixture (exit ${compile_result})\n"
    "stdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR
    "strict-alignment fixture failed at runtime (exit ${run_result})\n"
    "stdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
