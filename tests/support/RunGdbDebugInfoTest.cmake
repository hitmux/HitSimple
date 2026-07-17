function(require_success result output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}")
  endif()
endfunction()

foreach(variable HSC LLVM_DWARFDUMP GDB SOURCE)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "missing required ${variable} test argument")
  endif()
endforeach()

execute_process(
  COMMAND "${HSC}" -g "${SOURCE}" -o debug-info
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_error)
require_success("${compile_result}" "${compile_output}${compile_error}")

execute_process(
  COMMAND "${LLVM_DWARFDUMP}" --verify debug-info
  RESULT_VARIABLE dwarf_result
  OUTPUT_VARIABLE dwarf_output
  ERROR_VARIABLE dwarf_error)
require_success("${dwarf_result}" "${dwarf_output}${dwarf_error}")

execute_process(
  COMMAND readelf --debug-dump=info debug-info
  RESULT_VARIABLE readelf_result
  OUTPUT_VARIABLE readelf_output
  ERROR_VARIABLE readelf_error)
require_success("${readelf_result}" "${readelf_output}${readelf_error}")
if(NOT "${readelf_output}" MATCHES "DW_TAG_variable")
  message(FATAL_ERROR "debug-info is missing a DW_TAG_variable entry")
endif()

execute_process(
  COMMAND "${GDB}" -q -batch -ex "break debug_info.hs:6" -ex run -ex bt
          -ex "info locals" --args ./debug-info
  RESULT_VARIABLE gdb_result
  OUTPUT_VARIABLE gdb_output
  ERROR_VARIABLE gdb_error)
set(gdb_log "${gdb_output}${gdb_error}")
file(WRITE gdb.out "${gdb_log}")

if("${gdb_log}" MATCHES "ptrace: Operation not permitted")
  message(FATAL_ERROR "SKIP: ptrace is unavailable in this test environment")
endif()
require_success("${gdb_result}" "${gdb_log}")

foreach(expected "helper" "main" "value =")
  if(NOT "${gdb_log}" MATCHES "${expected}")
    message(FATAL_ERROR "GDB output is missing '${expected}':\n${gdb_log}")
  endif()
endforeach()
