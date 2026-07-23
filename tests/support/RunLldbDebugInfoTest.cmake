function(require_success result output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}")
  endif()
endfunction()

foreach(variable HSC LLVM_DWARFDUMP LLDB SOURCE)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "missing required ${variable} test argument")
  endif()
endforeach()

execute_process(
  COMMAND "${HSC}" -g --emit-object "${SOURCE}" -o debug-info.o
  RESULT_VARIABLE object_compile_result
  OUTPUT_VARIABLE object_compile_output
  ERROR_VARIABLE object_compile_error)
require_success("${object_compile_result}"
                "${object_compile_output}${object_compile_error}")

execute_process(
  COMMAND "${LLVM_DWARFDUMP}" --verify debug-info.o
  RESULT_VARIABLE object_dwarf_result
  OUTPUT_VARIABLE object_dwarf_output
  ERROR_VARIABLE object_dwarf_error)
require_success("${object_dwarf_result}"
                "${object_dwarf_output}${object_dwarf_error}")

execute_process(
  COMMAND "${LLVM_DWARFDUMP}" --debug-info debug-info.o
  RESULT_VARIABLE object_info_result
  OUTPUT_VARIABLE object_info_output
  ERROR_VARIABLE object_info_error)
require_success("${object_info_result}"
                "${object_info_output}${object_info_error}")
if(NOT "${object_info_output}" MATCHES "DW_TAG_variable")
  message(FATAL_ERROR "debug-info.o is missing a DW_TAG_variable entry")
endif()

execute_process(
  COMMAND "${HSC}" -g -O0 "${SOURCE}" -o debug-info-o0
  RESULT_VARIABLE o0_compile_result
  OUTPUT_VARIABLE o0_compile_output
  ERROR_VARIABLE o0_compile_error)
require_success("${o0_compile_result}" "${o0_compile_output}${o0_compile_error}")

execute_process(
  COMMAND "${LLVM_DWARFDUMP}" --verify debug-info-o0
  RESULT_VARIABLE o0_dwarf_result
  OUTPUT_VARIABLE o0_dwarf_output
  ERROR_VARIABLE o0_dwarf_error)
require_success("${o0_dwarf_result}" "${o0_dwarf_output}${o0_dwarf_error}")

execute_process(
  COMMAND "${LLDB}" -b
          -o "breakpoint set --file debug_info.hs --line 6"
          -o run
          -o bt
          -o "frame variable value"
          -- ./debug-info-o0
  RESULT_VARIABLE o0_lldb_result
  OUTPUT_VARIABLE o0_lldb_output
  ERROR_VARIABLE o0_lldb_error)
set(o0_lldb_log "${o0_lldb_output}${o0_lldb_error}")
file(WRITE lldb-o0.out "${o0_lldb_log}")
require_success("${o0_lldb_result}" "${o0_lldb_log}")

foreach(expected "helper" "main" "value =")
  if(NOT "${o0_lldb_log}" MATCHES "${expected}")
    message(FATAL_ERROR "O0 LLDB output is missing '${expected}':\n${o0_lldb_log}")
  endif()
endforeach()

execute_process(
  COMMAND "${HSC}" -g -O2 "${SOURCE}" -o debug-info-o2
  RESULT_VARIABLE o2_compile_result
  OUTPUT_VARIABLE o2_compile_output
  ERROR_VARIABLE o2_compile_error)
require_success("${o2_compile_result}" "${o2_compile_output}${o2_compile_error}")

execute_process(
  COMMAND "${LLVM_DWARFDUMP}" --verify debug-info-o2
  RESULT_VARIABLE o2_dwarf_result
  OUTPUT_VARIABLE o2_dwarf_output
  ERROR_VARIABLE o2_dwarf_error)
require_success("${o2_dwarf_result}" "${o2_dwarf_output}${o2_dwarf_error}")

execute_process(
  COMMAND "${LLDB}" -b
          -o "breakpoint set --file debug_info.hs --line 6"
          -o run
          -o bt
          -- ./debug-info-o2
  RESULT_VARIABLE o2_lldb_result
  OUTPUT_VARIABLE o2_lldb_output
  ERROR_VARIABLE o2_lldb_error)
set(o2_lldb_log "${o2_lldb_output}${o2_lldb_error}")
file(WRITE lldb-o2.out "${o2_lldb_log}")
require_success("${o2_lldb_result}" "${o2_lldb_log}")

foreach(expected "helper" "main")
  if(NOT "${o2_lldb_log}" MATCHES "${expected}")
    message(FATAL_ERROR "O2 LLDB output is missing '${expected}':\n${o2_lldb_log}")
  endif()
endforeach()
