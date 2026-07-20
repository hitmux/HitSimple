cmake_minimum_required(VERSION 3.24)

function(print_line text)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${text}")
endfunction()

if(NOT DEFINED BUILD_DIR OR BUILD_DIR STREQUAL "")
  message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED PARALLEL)
  set(PARALLEL 4)
endif()
if(NOT PARALLEL MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "PARALLEL must be a positive integer")
endif()

if(DEFINED CTEST_COMMAND AND NOT CTEST_COMMAND STREQUAL "")
  set(ctest_command "${CTEST_COMMAND}")
else()
  find_program(ctest_command NAMES ctest REQUIRED)
endif()

get_filename_component(build_dir "${BUILD_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${build_dir}")
  message(FATAL_ERROR "BUILD_DIR is not a directory: ${build_dir}")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef report_id)
set(junit_file "${build_dir}/ctest-quiet-${report_id}.xml")
set(ctest_arguments
  --test-dir "${build_dir}"
  --quiet
  --output-on-failure
  --output-junit "${junit_file}"
  --parallel "${PARALLEL}"
)
if(DEFINED TEST_REGEX AND NOT TEST_REGEX STREQUAL "")
  list(APPEND ctest_arguments --tests-regex "${TEST_REGEX}")
endif()

execute_process(
  COMMAND "${ctest_command}" ${ctest_arguments}
  RESULT_VARIABLE ctest_result
  OUTPUT_VARIABLE ctest_stdout
  ERROR_VARIABLE ctest_stderr
)

if(NOT EXISTS "${junit_file}")
  print_line("[FAIL] CTest did not produce a JUnit result report")
  if(NOT ctest_stdout STREQUAL "")
    print_line("${ctest_stdout}")
  endif()
  if(NOT ctest_stderr STREQUAL "")
    print_line("${ctest_stderr}")
  endif()
  message(FATAL_ERROR "CTest exited with status ${ctest_result}")
endif()

file(READ "${junit_file}" junit)
file(REMOVE "${junit_file}")

string(REGEX MATCH [[tests="([0-9]+)"]] matched_tests "${junit}")
set(total "${CMAKE_MATCH_1}")
string(REGEX MATCH [[failures="([0-9]+)"]] matched_failures "${junit}")
set(failures "${CMAKE_MATCH_1}")
string(REGEX MATCH [[disabled="([0-9]+)"]] matched_disabled "${junit}")
set(disabled "${CMAKE_MATCH_1}")
string(REGEX MATCH [[skipped="([0-9]+)"]] matched_skipped "${junit}")
set(skipped "${CMAKE_MATCH_1}")

if(total STREQUAL "" OR failures STREQUAL "" OR disabled STREQUAL "" OR
   skipped STREQUAL "")
  message(FATAL_ERROR "CTest produced an invalid JUnit result report")
endif()

math(EXPR passed "${total} - ${failures} - ${disabled} - ${skipped}")

if(NOT failures EQUAL 0)
  string(REGEX MATCHALL
    [[<testcase[^>]*status="fail"[^>]*>]]
    failed_headers
    "${junit}")
  foreach(failed_header IN LISTS failed_headers)
    string(REGEX MATCH [[name="([^"]*)"]] matched_name "${failed_header}")
    set(failed_test_name "${CMAKE_MATCH_1}")
    print_line("[FAIL] ${failed_test_name}")

    # JUnit records the failure state but some CTest versions omit the
    # assertion text when --quiet is active. Re-run just the failed test to
    # make its diagnostic available in CI logs.
    execute_process(
      COMMAND "${ctest_command}"
        --test-dir "${build_dir}"
        --output-on-failure
        --tests-regex "^${failed_test_name}$"
      OUTPUT_VARIABLE failed_test_stdout
      ERROR_VARIABLE failed_test_stderr
    )
    if(NOT failed_test_stdout STREQUAL "")
      print_line("${failed_test_stdout}")
    endif()
    if(NOT failed_test_stderr STREQUAL "")
      print_line("${failed_test_stderr}")
    endif()
  endforeach()

  string(REGEX MATCHALL
    [[<testcase[^>]*status="fail"[^>]*>[^<]*<failure[^>]*/>[^<]*(<properties/>[^<]*)?<system-out>[^<]*</system-out>[^<]*</testcase>]]
    failed_cases
    "${junit}")
  foreach(failed_case IN LISTS failed_cases)
    string(REGEX MATCH [[<system-out>([^<]*)</system-out>]] matched_output
      "${failed_case}")
    set(failure_output "${CMAKE_MATCH_1}")
    string(REPLACE "&quot;" "\"" failure_output "${failure_output}")
    string(REPLACE "&apos;" "'" failure_output "${failure_output}")
    string(REPLACE "&lt;" "<" failure_output "${failure_output}")
    string(REPLACE "&gt;" ">" failure_output "${failure_output}")
    string(REPLACE "&amp;" "&" failure_output "${failure_output}")
    string(STRIP "${failure_output}" failure_output)
    if(NOT failure_output STREQUAL "")
      print_line("${failure_output}")
    endif()
  endforeach()
endif()

set(summary "${passed}/${total} PASS")
if(NOT failures EQUAL 0)
  string(APPEND summary ", ${failures} FAIL")
endif()
if(NOT disabled EQUAL 0)
  string(APPEND summary ", ${disabled} DISABLED")
endif()
if(NOT skipped EQUAL 0)
  string(APPEND summary ", ${skipped} SKIP")
endif()
print_line("${summary}")

if(NOT ctest_result EQUAL 0)
  if(NOT ctest_stdout STREQUAL "")
    print_line("${ctest_stdout}")
  endif()
  if(NOT ctest_stderr STREQUAL "")
    print_line("${ctest_stderr}")
  endif()
  message(FATAL_ERROR "CTest failed")
endif()
