cmake_minimum_required(VERSION 3.24)

project(HitSimple
  VERSION 0.2.0
  LANGUAGES C CXX
)

include(GNUInstallDirs)

set(HITSIMPLE_PREBUILT_RUNTIME_LIBRARY "" CACHE FILEPATH
  "Use a prebuilt target runtime archive instead of compiling it with the compiler host toolchain")
set(HITSIMPLE_BOOST_LICENSE "" CACHE FILEPATH
  "Path to Boost LICENSE_1_0.txt for software f128 package attribution")
set(HITSIMPLE_LLVM_LICENSE "" CACHE FILEPATH
  "Path to the LLVM distribution license for packaged builds")
set(HITSIMPLE_RPM_RELEASE "1" CACHE STRING
  "RPM Release field, including a distribution suffix when required")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(BISON REQUIRED)
find_program(RE2C_EXECUTABLE re2c REQUIRED)
find_program(CLANG_EXECUTABLE NAMES clang-18 clang clang++ REQUIRED)

# LLVM packages may append host SDK paths. Propagate only actual LLVM headers.
set(HITSIMPLE_LLVM_INCLUDE_DIRS)
foreach(llvmIncludeDir IN LISTS LLVM_INCLUDE_DIRS)
  if(EXISTS "${llvmIncludeDir}/llvm/IR/Module.h")
    list(APPEND HITSIMPLE_LLVM_INCLUDE_DIRS "${llvmIncludeDir}")
  endif()
endforeach()
if(NOT HITSIMPLE_LLVM_INCLUDE_DIRS)
  message(FATAL_ERROR "LLVM package did not provide an LLVM header directory")
endif()

llvm_map_components_to_libnames(
  LLVM_LIBS
  Core
  Support
  Target
  TargetParser
  MC
  AsmPrinter
  native
)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Found Bison ${BISON_VERSION}")
message(STATUS "Found re2c: ${RE2C_EXECUTABLE}")
message(STATUS "Found clang: ${CLANG_EXECUTABLE}")

add_executable(hsc_mcpp
  third_party/mcpp/src/main.c
  third_party/mcpp/src/directive.c
  third_party/mcpp/src/eval.c
  third_party/mcpp/src/expand.c
  third_party/mcpp/src/mbchar.c
  third_party/mcpp/src/support.c
  third_party/mcpp/src/system.c
)

target_include_directories(hsc_mcpp
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/mcpp/src
)

target_compile_definitions(hsc_mcpp
  PRIVATE
    HAVE_CONFIG_H=0
)

if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
  target_compile_options(hsc_mcpp PRIVATE /w)
else()
  target_compile_options(hsc_mcpp PRIVATE -std=gnu89 -w)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(hsc_mcpp PRIVATE -Wno-incompatible-pointer-types)
  endif()
endif()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/Lexer.cpp
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND ${RE2C_EXECUTABLE}
          -o ${CMAKE_CURRENT_BINARY_DIR}/generated/Lexer.cpp
          ${PROJECT_SOURCE_DIR}/src/lexer/Lexer.re
  DEPENDS ${PROJECT_SOURCE_DIR}/src/lexer/Lexer.re
  VERBATIM
)

BISON_TARGET(HitSimpleParser
  ${PROJECT_SOURCE_DIR}/src/parser/Parser.y
  ${CMAKE_CURRENT_BINARY_DIR}/generated/Parser.cpp
  DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/generated/Parser.hpp
)

add_library(hitsimple_frontend STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/generated/Lexer.cpp
  ${BISON_HitSimpleParser_OUTPUTS}
  src/ast/AST.cpp
  src/compat/CCompatAst.cpp
  src/compat/CCompatExpressions.cpp
  src/compat/CCompatLexer.cpp
  src/compat/CCompatLowering.cpp
  src/compat/CCompatLoweringExpressions.cpp
  src/compat/CCompatLoweringOperators.cpp
  src/compat/CCompatLoweringStatements.cpp
  src/compat/CCompatParser.cpp
  src/compat/CCompatStatements.cpp
  src/codegen/LLVMCodegen.cpp
  src/codegen/LLVMCodegenCAbi.cpp
  src/codegen/LLVMCodegenControlFlow.cpp
  src/codegen/LLVMCodegenExpressions.cpp
  src/codegen/LLVMCodegenF128.cpp
  src/codegen/LLVMCodegenFunctions.cpp
  src/codegen/LLVMCodegenRuntime.cpp
  src/codegen/LLVMCodegenStatements.cpp
  src/diagnostic/Diagnostic.cpp
  src/hir/HIR.cpp
  src/hir/HIRDumper.cpp
  src/lexer/Token.cpp
  src/literal/Literal.cpp
  src/preprocessor/Preprocessor.cpp
  src/sema/Sema.cpp
  src/sema/SemaAnalysis.cpp
  src/sema/SemaAnalysisContext.cpp
  src/sema/SemaAssignments.cpp
  src/sema/SemaCalls.cpp
  src/sema/SemaExpressions.cpp
  src/sema/SemaOperators.cpp
  src/sema/SemaStatements.cpp
  src/stdlib/StandardLibrary.cpp
  src/support/Path.cpp
  src/support/Process.cpp
  src/support/ResourcePaths.cpp
  src/support/Toolchain.cpp
)

if(HITSIMPLE_PREBUILT_RUNTIME_LIBRARY)
  add_library(hitsimple_runtime STATIC IMPORTED GLOBAL)
  set_target_properties(hitsimple_runtime PROPERTIES
    IMPORTED_LOCATION "${HITSIMPLE_PREBUILT_RUNTIME_LIBRARY}"
  )
else()
  add_library(hitsimple_runtime STATIC runtime/hitsimple_runtime.c)

  if(WIN32 OR APPLE)
    find_package(Boost 1.85.0 REQUIRED)
    target_sources(hitsimple_runtime PRIVATE runtime/hitsimple_f128.cpp)
    target_include_directories(hitsimple_runtime PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(hitsimple_runtime
      PRIVATE HITSIMPLE_SOFTWARE_F128=1
    )
  else()
    target_sources(hitsimple_runtime PRIVATE runtime/hitsimple_f128_native.c)
  endif()

  set_target_properties(hitsimple_runtime PROPERTIES
    OUTPUT_NAME hitsimple_runtime
  )

  if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(hitsimple_runtime PRIVATE /W4)
  else()
    target_compile_options(hitsimple_runtime PRIVATE
      $<$<COMPILE_LANGUAGE:C>:-Wall>
      $<$<COMPILE_LANGUAGE:C>:-Wextra>
    )
  endif()
endif()

target_include_directories(hitsimple_frontend
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src/parser
)

target_include_directories(hitsimple_frontend
  SYSTEM
  PUBLIC
    ${HITSIMPLE_LLVM_INCLUDE_DIRS}
)

target_compile_definitions(hitsimple_frontend
  PUBLIC
    ${LLVM_DEFINITIONS}
    HITSIMPLE_VERSION="${PROJECT_VERSION}"
    HITSIMPLE_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    HITSIMPLE_MCPP_EXECUTABLE="$<TARGET_FILE:hsc_mcpp>"
    HITSIMPLE_RUNTIME_SOURCE="${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c"
    HITSIMPLE_RUNTIME_LIBRARY="$<TARGET_FILE:hitsimple_runtime>"
    HITSIMPLE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  target_compile_options(hitsimple_frontend PRIVATE /W4)
else()
  target_compile_options(hitsimple_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(hsc
  src/main.cpp
)

if(WIN32)
  target_sources(hsc_mcpp PRIVATE cmake/windows-utf8.manifest)
  target_sources(hsc PRIVATE cmake/windows-utf8.manifest)
  target_compile_definitions(hitsimple_frontend PUBLIC NOMINMAX)
  target_compile_definitions(hsc PRIVATE NOMINMAX)
endif()

target_include_directories(hsc
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(hsc
  PRIVATE
    HITSIMPLE_VERSION="${PROJECT_VERSION}"
    HITSIMPLE_LLVM_VERSION="${LLVM_PACKAGE_VERSION}"
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  target_compile_options(hsc PRIVATE /W4)
else()
  target_compile_options(hsc PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(hsc
  PRIVATE
    hitsimple_frontend
    ${LLVM_LIBS}
)

add_dependencies(hsc hsc_mcpp)
if(NOT HITSIMPLE_PREBUILT_RUNTIME_LIBRARY)
  add_dependencies(hsc hitsimple_runtime)
endif()

install(TARGETS hsc
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS hsc_mcpp
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/hitsimple
)
if(HITSIMPLE_PREBUILT_RUNTIME_LIBRARY)
  install(FILES "${HITSIMPLE_PREBUILT_RUNTIME_LIBRARY}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/hitsimple
    RENAME libhitsimple_runtime.a
  )
else()
  install(TARGETS hitsimple_runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/hitsimple
  )
endif()
install(DIRECTORY stdlib/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/hitsimple/stdlib
  FILES_MATCHING PATTERN "*.hsh"
)
install(FILES README.md USAGE.md Standard.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES third_party/mcpp/LICENSE
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  RENAME mcpp-LICENSE.txt
)

set(LLVM_DISTRIBUTION_COPYRIGHT
  "/usr/share/doc/llvm-${LLVM_VERSION_MAJOR}/copyright"
)
if(EXISTS "${LLVM_DISTRIBUTION_COPYRIGHT}")
  install(FILES "${LLVM_DISTRIBUTION_COPYRIGHT}"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME llvm-copyright.txt
  )
endif()
if(HITSIMPLE_LLVM_LICENSE AND EXISTS "${HITSIMPLE_LLVM_LICENSE}")
  install(FILES "${HITSIMPLE_LLVM_LICENSE}"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LLVM-LICENSE.txt
  )
endif()
if(EXISTS "/usr/share/common-licenses/Apache-2.0")
  install(FILES "/usr/share/common-licenses/Apache-2.0"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME Apache-2.0.txt
  )
endif()
if((WIN32 OR APPLE) AND HITSIMPLE_BOOST_LICENSE AND EXISTS "${HITSIMPLE_BOOST_LICENSE}")
  install(FILES "${HITSIMPLE_BOOST_LICENSE}"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME Boost-Software-License-1.0.txt
  )
elseif((WIN32 OR APPLE) AND EXISTS "${Boost_INCLUDE_DIRS}/../LICENSE_1_0.txt")
  install(FILES "${Boost_INCLUDE_DIRS}/../LICENSE_1_0.txt"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME Boost-Software-License-1.0.txt
  )
endif()

include(CTest)

if(BUILD_TESTING)
  add_executable(hsc_unit_tests
    tests/test_main.cpp
    tests/diagnostic/SourceLocationTests.cpp
    tests/codegen/LLVMCodegenTests.cpp
    tests/compat/CCompatTests.cpp
    tests/lexer/LexerTests.cpp
    tests/parser/ParserTests.cpp
    tests/preprocessor/PreprocessorTests.cpp
    tests/sema/SemaAggregateTests.cpp
    tests/sema/SemaCoreTests.cpp
    tests/sema/SemaDiagnosticTests.cpp
    tests/sema/SemaExpressionTests.cpp
    tests/sema/SemaTryCatchTests.cpp
    tests/support/ProcessTests.cpp
    tests/support/ResourcePathsTests.cpp
    tests/support/TestRunner.cpp
  )

  target_include_directories(hsc_unit_tests
    PRIVATE
      ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_SOURCE_DIR}/tests
      ${CMAKE_CURRENT_BINARY_DIR}/generated
  )

  if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(hsc_unit_tests PRIVATE /W4)
  else()
    target_compile_options(hsc_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()

target_link_libraries(hsc_unit_tests
  PRIVATE
    hitsimple_frontend
    ${LLVM_LIBS}
)

  add_dependencies(hsc_unit_tests hsc_mcpp)
  if(NOT HITSIMPLE_PREBUILT_RUNTIME_LIBRARY)
    add_dependencies(hsc_unit_tests hitsimple_runtime)
  endif()

  set(HSC_CTEST_OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ctest-output")

  function(add_hsc_shell_test name shell_command)
    set(output_dir "${HSC_CTEST_OUTPUT_ROOT}/${name}")
    file(MAKE_DIRECTORY "${output_dir}")
    if(WIN32)
      add_test(NAME "${name}" COMMAND ${CMAKE_COMMAND} -E true)
      set_tests_properties("${name}" PROPERTIES DISABLED TRUE)
    else()
      add_test(NAME "${name}" COMMAND /bin/sh -c "${shell_command}")
    endif()
    set_tests_properties("${name}" PROPERTIES WORKING_DIRECTORY "${output_dir}")
  endfunction()

  add_test(NAME unit_tests COMMAND hsc_unit_tests)
  if(APPLE AND NOT HITSIMPLE_PREBUILT_RUNTIME_LIBRARY)
    add_executable(hitsimple_f128_runtime_tests
      tests/runtime/F128RuntimeTests.cpp
    )
    target_link_libraries(hitsimple_f128_runtime_tests
      PRIVATE hitsimple_runtime
    )
    add_test(NAME software_f128_runtime COMMAND hitsimple_f128_runtime_tests)
  endif()
  add_test(NAME hsc_help COMMAND hsc --help)
  add_test(NAME hsc_version COMMAND hsc --version)
  add_test(NAME hsc_target_info COMMAND hsc --target-info)
  add_hsc_shell_test(hsc_target_info_documents_main_abi
    "$<TARGET_FILE:hsc> --target-info | grep -Fx 'abi.main: unannotated main uses i32, fallthrough returns 0'"
  )
  if(WIN32 OR APPLE)
    set(HITSIMPLE_EXPECTED_F128_BACKEND
      "boost.cpp_bin_float.113-bit-software-binary128")
  else()
    set(HITSIMPLE_EXPECTED_F128_BACKEND "native.fp128")
  endif()
  add_hsc_shell_test(hsc_target_info_documents_checked_and_calloc_status
    "$<TARGET_FILE:hsc> --target-info > target-info.out && grep -Fq 'runtime.kind: static-library' target-info.out && grep -Fq 'f128.backend: ${HITSIMPLE_EXPECTED_F128_BACKEND}' target-info.out && grep -Fq 'stdlib.headers: stdlib.hsh, string.hsh, stdio.hsh, math.hsh, ctype.hsh, time.hsh, and assert.hsh; each standard function requires its owning system header' target-info.out && grep -Fq 'checked.address_coverage: local objects are removed when their function frame exits; internal globals/statics persist; extern globals, FFI raw addresses, and file handles are not registered' target-info.out && grep -Fq 'stdlib.formatting: native printf, print, fprintf, scanf, and fscanf use runtime descriptors; C compatibility does not accept variadic declarations' target-info.out && grep -Fq 'stdlib.raw_output: print(view as none), put, and fput write the complete View and return the written byte count' target-info.out && grep -Fq 'float.fallback: f16 math evaluates as f32 and rounds back to binary16; f128 uses the target-specific runtime backend' target-info.out && grep -Fq 'abi.c_compat.aggregate_by_value: only x86_64 SysV ELF targets; other targets reject C struct by-value parameters and returns; supported direct aggregates up to 16 bytes use at most two INTEGER/SSE eightbytes' target-info.out && grep -Fq 'abi.c_compat.aggregate_indirect: on supported x86_64 SysV ELF targets, aggregates larger than 16 bytes use sret returns and byval parameters' target-info.out && grep -Fq 'abi.c_compat.aggregate_limits:' target-info.out && grep -Fq 'C compatibility validates external ABI shapes before link' target-info.out"
  )
  add_test(NAME hsc_compile_with_explicit_clang
    COMMAND hsc --clang ${CLANG_EXECUTABLE} ${PROJECT_SOURCE_DIR}/examples/hello.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/explicit_clang_smoke
  )
  add_hsc_shell_test(hsc_emit_llvm_does_not_require_clang
    "$<TARGET_FILE:hsc> --clang ${CMAKE_CURRENT_BINARY_DIR}/missing-clang --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs -o module.ll && test -s module.ll"
  )
  add_hsc_shell_test(hsc_reports_missing_clang_before_link
    "! env PATH= $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/examples/hello.hs -o program >stdout 2>stderr && grep -Fq 'no compatible Clang toolchain found' stderr"
  )
  add_hsc_shell_test(hsc_missing_input_fails
    "! $<TARGET_FILE:hsc> 2>stderr && grep -Fq 'hsc: missing input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multiple_actions
    "! $<TARGET_FILE:hsc> --emit-llvm --dump-ast ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'multiple action options' stderr"
  )
  add_hsc_shell_test(hsc_rejects_dump_output_path
    "! $<TARGET_FILE:hsc> --dump-tokens -o ignored-output ${PROJECT_SOURCE_DIR}/tests/cases/lexer/typed_operators.hs >stdout 2>stderr && grep -Fq -- '-o is not supported with --dump-tokens' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_dump_tokens
    "! $<TARGET_FILE:hsc> --c-compat --dump-tokens ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq -- '--dump-tokens is not supported with --c-compat' stderr"
  )
  add_hsc_shell_test(hsc_rejects_missing_output_directory
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/examples/hello.hs -o ${CMAKE_CURRENT_BINARY_DIR}/missing-output-dir/out 2>stderr && grep -Fq 'output directory does not exist' stderr"
  )
  add_test(NAME hsc_dump_tokens
    COMMAND hsc --dump-tokens ${PROJECT_SOURCE_DIR}/tests/cases/lexer/typed_operators.hs
  )
  add_hsc_shell_test(hsc_dump_tokens_reports_located_invalid_token
    "! $<TARGET_FILE:hsc> --dump-tokens ${PROJECT_SOURCE_DIR}/tests/cases/lexer/invalid_token.hs >stdout 2>stderr && grep -Fq ':1:1: lexer: error: invalid token `@`' stderr"
  )
  add_test(NAME hsc_preprocess_only
    COMMAND hsc --preprocess-only ${PROJECT_SOURCE_DIR}/tests/cases/preprocessor/main.hs
  )
  add_hsc_shell_test(hsc_rejects_multifile_dump_tokens
    "! $<TARGET_FILE:hsc> --dump-tokens ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs >stdout 2>stderr && grep -Fq -- '--dump-tokens supports exactly one input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multifile_preprocess_only
    "! $<TARGET_FILE:hsc> -E ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs >stdout 2>stderr && grep -Fq -- '--preprocess-only supports exactly one input file' stderr"
  )
  add_test(NAME hsc_emit_llvm
    COMMAND hsc ${PROJECT_SOURCE_DIR}/examples/hello.hs --emit-llvm
  )
  add_hsc_shell_test(hsc_c_compat_double_abi_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_double_abi.c > module.ll && grep -Fq 'declare double @host_value()' module.ll && grep -Fq 'define double @add(double' module.ll && grep -Fq 'call double @host_value()' module.ll && grep -Fq 'call double @add(double' module.ll"
  )
  add_hsc_shell_test(hsc_c_compat_double_compare_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_runtime_double.c > module.ll && grep -Fq 'fcmp oeq double' module.ll && grep -Fq 'fcmp une double' module.ll && grep -Fq 'fcmp olt double' module.ll && grep -Fq 'fcmp ole double' module.ll && grep -Fq 'fcmp ogt double' module.ll && grep -Fq 'fcmp oge double' module.ll"
  )
  add_hsc_shell_test(hsc_c_compat_external_symbol_abi_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_abi_symbols.c > module.ll && grep -Fq 'declare i32 @puts(ptr)' module.ll && grep -Fq 'declare double @atof(ptr)' module.ll"
  )

  set(HSC_C_COMPAT_LINKER "${CLANG_EXECUTABLE}")
  set(HSC_C_COMPAT_RUNTIME_PREFIX
    "-x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c")
  set(HSC_C_COMPAT_RUNTIME_SUFFIX "")
  if(WIN32 OR APPLE)
    set(HSC_C_COMPAT_LINKER "${CMAKE_CXX_COMPILER}")
    set(HSC_C_COMPAT_RUNTIME_PREFIX "")
    set(HSC_C_COMPAT_RUNTIME_SUFFIX "-x none $<TARGET_FILE:hitsimple_runtime>")
  endif()
  if(APPLE)
    set(HSC_C_COMPAT_LINKER
      "${CMAKE_CXX_COMPILER} -isysroot $(xcrun --show-sdk-path)")
  endif()

  add_hsc_shell_test(hsc_c_compat_native_scalar_abi
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_native_scalar_abi.c -o module.ll && ${HSC_C_COMPAT_LINKER} -x ir module.ll ${HSC_C_COMPAT_RUNTIME_PREFIX} -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_native_scalar_abi_helper.c ${HSC_C_COMPAT_RUNTIME_SUFFIX} -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_native_scalar_void_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_native_scalar_void_library.c -o module.ll && ${HSC_C_COMPAT_LINKER} -x ir module.ll ${HSC_C_COMPAT_RUNTIME_PREFIX} -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_native_scalar_void_caller.c ${HSC_C_COMPAT_RUNTIME_SUFFIX} -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_array_typedef_runtime
    "$<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_array_typedef.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_arrow_array_member_runtime
    "$<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_arrow_array_member.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_undefined_static_prototype
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_prototype_without_definition.c -o program 2>stderr && grep -Fq 'requires a definition in the same translation unit' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_static_prototype_satisfied_by_external_tu
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_prototype_without_definition.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_prototype_external_definition.c -o program 2>stderr && grep -Fq 'requires a definition in the same translation unit' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_incompatible_function_prototype
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/incompatible_function_prototype.c -o program 2>stderr && grep -Fq 'conflicting C function declaration' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_incompatible_tu_declaration
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/incompatible_tu_main.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/incompatible_tu_definition.c -o program 2>stderr && grep -Fq 'incompatible C external declaration' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_incompatible_global_tu_declaration
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/incompatible_global_tu_main.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/incompatible_global_tu_definition.c -o program 2>stderr && grep -Fq 'incompatible C external declaration' stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_cross_tu_typedef_leak
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/tu_isolation_typedef_a.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/tu_isolation_typedef_b.c -o program >stdout 2>stderr && grep -Fq TuPrivateAlias stderr"
  )
  add_hsc_shell_test(hsc_rejects_cross_tu_macro_leak
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/tu_isolation_macro_a.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/tu_isolation_macro_b.hs -o program >stdout 2>stderr && grep -Fq TU_PRIVATE_VALUE stderr"
  )
  add_hsc_shell_test(hsc_rejects_c_compat_variadic_declaration
    "! $<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_variadic_declaration.c -o program >stdout 2>stderr && grep -Fq 'expected C type' stderr"
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  add_hsc_shell_test(hsc_c_compat_integer_triplet_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_integer_triplet_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_integer_triplet_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_integer_triplet_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_integer_triplet_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_integer_triplet_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_float_triplet_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_triplet_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_triplet_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_float_triplet_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_triplet_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_triplet_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_double_float_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_double_float_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_double_float_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_double_float_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_double_float_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_double_float_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_mixed_abi_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_library.c -o module.ll && grep -Fq 'define { i32, double } @pass_mix(i32' module.ll"
  )
  add_hsc_shell_test(hsc_c_compat_pair_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_pair_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_pair_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_pair_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_mixed_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_mixed_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_float_pair_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_pair_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_pair_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_float_pair_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_pair_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_float_pair_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_weighted_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_floating_by_value.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_floating_by_value_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_weighted_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_weighted_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_weighted_native_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_mixed_indirect_hsc_to_native
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_indirect_hsc_to_native.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_indirect_native.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_compat_mixed_indirect_native_to_hsc
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_indirect_library.c -o module.ll && ${CLANG_EXECUTABLE} -x ir module.ll -x c ${PROJECT_SOURCE_DIR}/runtime/hitsimple_runtime.c -x c ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_aggregate_mixed_indirect_native_caller.c -o program && ./program"
  )
  endif()
  add_hsc_shell_test(hsc_emit_llvm_library_translation_unit
    "$<TARGET_FILE:hsc> --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs > module.ll && grep -Fq '@add_two' module.ll && ! grep -Fq '@main' module.ll"
  )
  add_hsc_shell_test(hsc_rejects_multifile_emit_llvm
    "! $<TARGET_FILE:hsc> --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs >stdout 2>stderr && grep -Fq -- '--emit-llvm supports exactly one input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multifile_dump_ast
    "! $<TARGET_FILE:hsc> --dump-ast ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs >stdout 2>stderr && grep -Fq -- '--dump-ast supports exactly one input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multifile_dump_hir
    "! $<TARGET_FILE:hsc> --dump-hir ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs >stdout 2>stderr && grep -Fq -- '--dump-hir supports exactly one input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multifile_duplicate_main
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/duplicate_main_a.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/duplicate_main_b.hs -o program 2>stderr && grep -Fq 'only one main' stderr"
  )
  add_hsc_shell_test(hsc_rejects_duplicate_main_within_translation_unit
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/duplicate_main_same_unit.hs -o program 2>stderr && grep -Fq 'only one main' stderr"
  )
  add_hsc_shell_test(hsc_rejects_multifile_duplicate_external_function
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/duplicate_function_a.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/duplicate_function_b.hs -o program 2>stderr && grep -Fq 'Clang failed while linking executable' stderr"
  )
  add_hsc_shell_test(hsc_rejects_program_without_main
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs -o program 2>stderr && grep -Fq 'program must define a main function' stderr"
  )
  add_hsc_shell_test(hsc_rejects_undeclared_cross_tu_function
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/undeclared_main.hs -o program 2>stderr && grep -Fq 'unsupported function call' stderr"
  )
  add_test(NAME hsc_static_checked_emit_llvm
    COMMAND hsc --static-checked ${PROJECT_SOURCE_DIR}/examples/hello.hs --emit-llvm
  )
  add_hsc_shell_test(hsc_run_printf_calloc_string_unchecked
    "$<TARGET_FILE:hsc> --unchecked ${PROJECT_SOURCE_DIR}/tests/cases/run/printf_calloc_string.hs -o ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_unchecked && ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_unchecked"
  )
  add_hsc_shell_test(hsc_run_printf_calloc_string_checked
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/printf_calloc_string.hs -o ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_checked && ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_checked"
  )
  add_hsc_shell_test(hsc_run_printf_calloc_string_static_checked
    "$<TARGET_FILE:hsc> --static-checked ${PROJECT_SOURCE_DIR}/tests/cases/run/printf_calloc_string.hs -o ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_static_checked && ${CMAKE_CURRENT_BINARY_DIR}/printf_calloc_string_static_checked"
  )
  add_hsc_shell_test(hsc_run_checked_memory_bridge
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_memory_bridge.hs -o ${CMAKE_CURRENT_BINARY_DIR}/checked_memory_bridge && ${CMAKE_CURRENT_BINARY_DIR}/checked_memory_bridge"
  )
  add_test(NAME hsc_compile_hello
    COMMAND hsc ${PROJECT_SOURCE_DIR}/examples/hello.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello_smoke
  )
  add_test(NAME hsc_run_hello
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/hello_smoke
  )
  add_test(NAME hsc_compile_arithmetic
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/arithmetic.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/arithmetic_smoke
  )
  add_test(NAME hsc_run_arithmetic
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/arithmetic_smoke
  )
  add_test(NAME hsc_compile_while
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/while.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/while_smoke
  )
  add_test(NAME hsc_run_while
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/while_smoke
  )
  add_test(NAME hsc_compile_compound_assignment
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/compound_assignment.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/compound_assignment_smoke
  )
  add_test(NAME hsc_run_compound_assignment
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/compound_assignment_smoke
  )
  add_test(NAME hsc_compile_functions
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/functions.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/functions_smoke
  )
  add_test(NAME hsc_run_functions
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/functions_smoke
  )
  add_test(NAME hsc_compile_multifile
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs
            ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/multifile_smoke
  )
  add_test(NAME hsc_run_multifile
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/multifile_smoke
  )
  add_test(NAME hsc_compile_multifile_reverse
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs
            ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/multifile_reverse_smoke
  )
  add_test(NAME hsc_run_multifile_reverse
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/multifile_reverse_smoke
  )
  add_hsc_shell_test(hsc_run_global_initializer
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/global_initializer.hs -o ${CMAKE_CURRENT_BINARY_DIR}/global_initializer_smoke && ${CMAKE_CURRENT_BINARY_DIR}/global_initializer_smoke"
  )
  add_hsc_shell_test(hsc_run_c_compat_double
    "$<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_runtime_double.c -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_double_smoke && ${CMAKE_CURRENT_BINARY_DIR}/c_compat_double_smoke"
  )
  add_hsc_shell_test(hsc_run_c_compat_static_tu_and_extern_global
    "$<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_tu_a.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_tu_b.c -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_static_tu_smoke && ${CMAKE_CURRENT_BINARY_DIR}/c_compat_static_tu_smoke"
  )
  add_hsc_shell_test(hsc_run_c_compat_static_prototype_inherits_internal_linkage
    "$<TARGET_FILE:hsc> --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_prototype_inherited_a.c ${PROJECT_SOURCE_DIR}/tests/cases/compat/static_prototype_inherited_b.c -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_static_prototype_inherited_smoke && ${CMAKE_CURRENT_BINARY_DIR}/c_compat_static_prototype_inherited_smoke"
  )
  add_test(NAME hsc_compile_control_h
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/control_h.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/control_h_smoke
  )
  add_test(NAME hsc_run_control_h
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/control_h_smoke
  )
  add_hsc_shell_test(hsc_run_try_catch_float_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_float_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_float_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_float_view_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_user_template_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_user_template_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_user_template_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_user_template_view_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_nested_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_nested_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_nested_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_nested_view_smoke"
  )
  add_hsc_shell_test(hsc_rejects_incompatible_try_catch_view
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_incompatible_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_incompatible_view_smoke 2>stderr && grep -Fq 'throw View byte length 8 does not match catch byte length 4' stderr"
  )
  add_hsc_shell_test(hsc_run_try_catch_float_view_checked
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_float_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_float_view_checked_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_float_view_checked_smoke"
  )
  add_hsc_shell_test(hsc_run_uncaught_try_catch_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_uncaught_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_uncaught_view_smoke && ! ${CMAKE_CURRENT_BINARY_DIR}/try_catch_uncaught_view_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_i32_to_addr_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_i32_to_addr_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_i32_to_addr_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_i32_to_addr_view_smoke"
  )
  add_hsc_shell_test(hsc_rejects_try_catch_integer_to_float
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_integer_to_float_rejected.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_integer_to_float_rejected_smoke 2>stderr && grep -Fq 'float operand is not a float expression' stderr"
  )
  add_hsc_shell_test(hsc_run_try_catch_function_template_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_function_template_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_function_template_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_function_template_view_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_rethrow_outer
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_rethrow_outer.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_rethrow_outer_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_rethrow_outer_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_rethrow_uncaught
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_rethrow_uncaught.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_rethrow_uncaught_smoke && ! ${CMAKE_CURRENT_BINARY_DIR}/try_catch_rethrow_uncaught_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_cstr_view
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_cstr_view.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_cstr_view_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_cstr_view_smoke"
  )
  add_hsc_shell_test(hsc_run_try_catch_user_template_assignment
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_user_template_assignment.hs -o ${CMAKE_CURRENT_BINARY_DIR}/try_catch_user_template_assignment_smoke && ${CMAKE_CURRENT_BINARY_DIR}/try_catch_user_template_assignment_smoke"
  )
  add_test(NAME hsc_compile_structs
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/structs.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/structs_smoke
  )
  add_test(NAME hsc_run_structs
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/structs_smoke
  )
  add_test(NAME hsc_compile_checked_oob
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_oob.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_oob_smoke
  )
  add_test(NAME hsc_run_checked_oob
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/checked_oob_smoke
  )
  add_test(NAME hsc_compile_checked_double_free
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_double_free.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_double_free_smoke
  )
  add_test(NAME hsc_run_checked_double_free
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/checked_double_free_smoke
  )
  add_test(NAME hsc_compile_checked_use_after_free
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_use_after_free.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_use_after_free_smoke
  )
  add_test(NAME hsc_run_checked_use_after_free
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/checked_use_after_free_smoke
  )
  add_test(NAME hsc_compile_preprocessor
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/preprocessor/main.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/preprocessor_smoke
  )
  add_test(NAME hsc_run_preprocessor
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/preprocessor_smoke
  )
  add_test(NAME hsc_compile_stdlib_core
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/stdlib_core.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/stdlib_core_smoke
  )
  add_test(NAME hsc_run_stdlib_core
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/stdlib_core_smoke
  )
  add_test(NAME hsc_compile_example_stdlib_core
    COMMAND hsc ${PROJECT_SOURCE_DIR}/examples/stdlib_core.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/example_stdlib_core
  )
  add_test(NAME hsc_run_example_stdlib_core
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/example_stdlib_core
  )
  add_test(NAME hsc_compile_comprehensive_project_unchecked
    COMMAND hsc --unchecked ${PROJECT_SOURCE_DIR}/examples/comprehensive_project.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_unchecked
  )
  add_test(NAME hsc_run_comprehensive_project_unchecked
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_unchecked
  )
  add_test(NAME hsc_compile_comprehensive_project_static_checked
    COMMAND hsc --static-checked ${PROJECT_SOURCE_DIR}/examples/comprehensive_project.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_static_checked
  )
  add_test(NAME hsc_run_comprehensive_project_static_checked
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_static_checked
  )
  add_test(NAME hsc_compile_comprehensive_project_checked
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/examples/comprehensive_project.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_checked
  )
  add_test(NAME hsc_run_comprehensive_project_checked
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/comprehensive_project_checked
  )
  add_test(NAME hsc_compile_multi_return
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/multi_return.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/multi_return_smoke
  )
  add_test(NAME hsc_run_multi_return
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/multi_return_smoke
  )
  add_test(NAME hsc_compile_static_global
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/static_global.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/static_global_smoke
  )
  add_test(NAME hsc_run_static_global
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/static_global_smoke
  )
  add_test(NAME hsc_compile_pointer_slice
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/pointer_slice.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/pointer_slice_smoke
  )
  add_test(NAME hsc_run_pointer_slice
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/pointer_slice_smoke
  )
  add_test(NAME hsc_compile_struct_member_offsets
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/struct_member_offsets.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/struct_member_offsets_smoke
  )
  add_test(NAME hsc_run_struct_member_offsets
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/struct_member_offsets_smoke
  )
  add_test(NAME hsc_compile_member_template_override
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/member_template_override.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/member_template_override_smoke
  )
  add_test(NAME hsc_run_member_template_override
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/member_template_override_smoke
  )
  add_test(NAME hsc_compile_template_view
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/template_view.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/template_view_smoke
  )
  add_test(NAME hsc_run_template_view
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/template_view_smoke
  )
  add_test(NAME hsc_compile_user_template_ops
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_ops.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_ops_smoke
  )
  add_test(NAME hsc_run_user_template_ops
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/user_template_ops_smoke
  )
  add_test(NAME hsc_compile_user_template_format
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_format.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_format_smoke
  )
  add_hsc_shell_test(hsc_run_user_template_format
    "${CMAKE_CURRENT_BINARY_DIR}/user_template_format_smoke >stdout && grep -Fx 'Vec2' stdout"
  )
  add_test(NAME hsc_compile_user_template_format_fprintf
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_format_fprintf.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_format_fprintf_smoke
  )
  add_hsc_shell_test(hsc_run_user_template_format_fprintf
    "${CMAKE_CURRENT_BINARY_DIR}/user_template_format_fprintf_smoke && test \"$(cat user_template_format_fprintf.out)\" = 'F'"
  )
  add_test(NAME hsc_compile_user_template_format_fprintf_expr
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_format_fprintf_expr.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_format_fprintf_expr_smoke
  )
  add_hsc_shell_test(hsc_run_user_template_format_fprintf_expr
    "${CMAKE_CURRENT_BINARY_DIR}/user_template_format_fprintf_expr_smoke && test \"$(cat user_template_format_fprintf_expr.out)\" = 'E'"
  )
  add_test(NAME hsc_compile_user_template_format_negative
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_format_negative.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_format_negative_smoke
  )
  add_hsc_shell_test(hsc_run_user_template_format_negative
    "${CMAKE_CURRENT_BINARY_DIR}/user_template_format_negative_smoke; test $? -eq 249"
  )
  add_test(NAME hsc_compile_user_template_format_printf
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/user_template_format_printf.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/user_template_format_printf_smoke
  )
  add_hsc_shell_test(hsc_run_user_template_format_printf
    "${CMAKE_CURRENT_BINARY_DIR}/user_template_format_printf_smoke >stdout && grep -Fx 'printf-sink' stdout"
  )
  add_test(NAME hsc_compile_impl_methods
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/impl_methods.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/impl_methods_smoke
  )
  add_test(NAME hsc_run_impl_methods
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/impl_methods_smoke
  )
  add_test(NAME hsc_compile_impl_method_value_copy
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/impl_method_value_copy.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/impl_method_value_copy_smoke
  )
  add_test(NAME hsc_run_impl_method_value_copy
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/impl_method_value_copy_smoke
  )
  add_test(NAME hsc_compile_impl_method_void
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/impl_method_void.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/impl_method_void_smoke
  )
  add_test(NAME hsc_run_impl_method_void
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/impl_method_void_smoke
  )
  add_test(NAME hsc_compile_increment_lvalues
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/increment_lvalues.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/increment_lvalues_smoke
  )
  add_test(NAME hsc_run_increment_lvalues
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/increment_lvalues_smoke
  )
  add_test(NAME hsc_compile_scanf_left_context
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/scanf_left_context.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/scanf_left_context_smoke
  )
  add_hsc_shell_test(hsc_run_scanf_left_context
    "printf '42\\n' | ${CMAKE_CURRENT_BINARY_DIR}/scanf_left_context_smoke"
  )
  add_test(NAME hsc_compile_scanf_dynamic_format
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/scanf_dynamic_format.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/scanf_dynamic_format_smoke
  )
  add_hsc_shell_test(hsc_run_scanf_dynamic_format
    "printf '42\\n' | ${CMAKE_CURRENT_BINARY_DIR}/scanf_dynamic_format_smoke"
  )
  add_test(NAME hsc_compile_scanf_partial_success
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/scanf_partial_success.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/scanf_partial_success_smoke
  )
  add_hsc_shell_test(hsc_run_scanf_partial_success
    "printf '7 x\\n' | ${CMAKE_CURRENT_BINARY_DIR}/scanf_partial_success_smoke"
  )
  add_test(NAME hsc_compile_checked_scanf_dynamic_format_error
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_scanf_dynamic_format_error.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_scanf_dynamic_format_error_smoke
  )
  add_hsc_shell_test(hsc_run_checked_scanf_dynamic_format_error
    "! ${CMAKE_CURRENT_BINARY_DIR}/checked_scanf_dynamic_format_error_smoke >stdout 2>stderr && grep -Fq 'hitsimple runtime error: unknown scan format specifier' stderr"
  )
  add_test(NAME hsc_compile_checked_printf_dynamic_float
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_printf_dynamic_float.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_printf_dynamic_float_smoke
  )
  add_hsc_shell_test(hsc_run_checked_printf_dynamic_float
    "${CMAKE_CURRENT_BINARY_DIR}/checked_printf_dynamic_float_smoke >stdout && grep -Fx '1.5' stdout"
  )
  add_test(NAME hsc_compile_checked_dynamic_float_statement
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_dynamic_float_statement.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_dynamic_float_statement_smoke
  )
  add_hsc_shell_test(hsc_run_checked_dynamic_float_statement
    "trap 'rm -f checked_dynamic_float_statement.tmp' EXIT; ${CMAKE_CURRENT_BINARY_DIR}/checked_dynamic_float_statement_smoke >stdout && grep -Fx '1.5' stdout && grep -Fx '1.5' checked_dynamic_float_statement.tmp"
  )
  add_test(NAME hsc_compile_floating_returns
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/floating_returns.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/floating_returns_smoke
  )
  add_test(NAME hsc_run_floating_returns
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/floating_returns_smoke
  )
  add_test(NAME hsc_compile_f16_f128_fallback
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/f16_f128_fallback.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/f16_f128_fallback_smoke
  )
  add_test(NAME hsc_run_f16_f128_fallback
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/f16_f128_fallback_smoke
  )
  add_test(NAME hsc_compile_checked_internal_global_pointer
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_internal_global_pointer.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_internal_global_pointer_smoke
  )
  add_test(NAME hsc_run_checked_internal_global_pointer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/checked_internal_global_pointer_smoke
  )
  add_test(NAME hsc_compile_checked_static_local_pointer
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_static_local_pointer.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_static_local_pointer_smoke
  )
  add_test(NAME hsc_run_checked_static_local_pointer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/checked_static_local_pointer_smoke
  )
  add_test(NAME hsc_compile_checked_expired_local_return
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_expired_local_return.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_expired_local_return_smoke
  )
  add_hsc_shell_test(hsc_run_checked_expired_local_return
    "! ${CMAKE_CURRENT_BINARY_DIR}/checked_expired_local_return_smoke >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid checked load' stderr"
  )
  add_test(NAME hsc_compile_checked_expired_local_fallthrough
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_expired_local_fallthrough.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/checked_expired_local_fallthrough_smoke
  )
  add_hsc_shell_test(hsc_run_checked_expired_local_fallthrough
    "! ${CMAKE_CURRENT_BINARY_DIR}/checked_expired_local_fallthrough_smoke >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid checked load' stderr"
  )
  add_test(NAME hsc_compile_handle_io
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/handle_io.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/handle_io_smoke
  )
  add_hsc_shell_test(hsc_run_handle_io
    "${CMAKE_CURRENT_BINARY_DIR}/handle_io_smoke >stdout; test $? -eq 1"
  )
  add_test(NAME hsc_compile_c_compat_unsigned_integer
    COMMAND hsc --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_unsigned_integer.c
            -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_unsigned_integer_smoke
  )
  add_test(NAME hsc_run_c_compat_unsigned_integer
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/c_compat_unsigned_integer_smoke
  )
  add_hsc_shell_test(hsc_c_compat_unsigned_integer_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_unsigned_integer.c > module.ll && grep -Fq 'icmp ult i32' module.ll && grep -Fq 'lshr i32' module.ll && grep -Fq 'lshr i64' module.ll"
  )
  add_test(NAME hsc_compile_c_compat_integer_conversion_matrix
    COMMAND hsc --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_integer_conversion_matrix.c
            -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_integer_conversion_matrix_smoke
  )
  add_test(NAME hsc_run_c_compat_integer_conversion_matrix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/c_compat_integer_conversion_matrix_smoke
  )
  add_hsc_shell_test(hsc_c_compat_integer_conversion_matrix_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_integer_conversion_matrix.c -o module.ll && grep -Fq 'trunc i64' module.ll && grep -Fq 'sext i8' module.ll && grep -Fq 'zext i8' module.ll"
  )
  add_test(NAME hsc_compile_c_compat_integer_shift_matrix
    COMMAND hsc --c-compat ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_integer_shift_matrix.c
            -o ${CMAKE_CURRENT_BINARY_DIR}/c_compat_integer_shift_matrix_smoke
  )
  add_test(NAME hsc_run_c_compat_integer_shift_matrix
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/c_compat_integer_shift_matrix_smoke
  )
  add_hsc_shell_test(hsc_c_compat_integer_shift_matrix_emit_llvm
    "$<TARGET_FILE:hsc> --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_integer_shift_matrix.c -o module.ll && grep -Fq 'shl i32' module.ll && grep -Fq 'lshr i32' module.ll && grep -Fq 'ashr i64' module.ll && grep -Fq 'lshr i64' module.ll"
  )
  add_test(NAME hsc_compile_checked_pointer_slice
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/pointer_slice.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/pointer_slice_checked_smoke
  )
  add_test(NAME hsc_run_checked_pointer_slice
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/pointer_slice_checked_smoke
  )
  add_test(NAME hsc_compile_checked_scanf_left_context
    COMMAND hsc --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/scanf_left_context.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/scanf_left_context_checked_smoke
  )
  add_hsc_shell_test(hsc_run_checked_scanf_left_context
    "printf '42\\n' | ${CMAKE_CURRENT_BINARY_DIR}/scanf_left_context_checked_smoke"
  )

  set_tests_properties(hsc_compile_hello
    PROPERTIES FIXTURES_SETUP hello_executable
  )
  set_tests_properties(hsc_run_hello
    PROPERTIES FIXTURES_REQUIRED hello_executable
  )
  set_tests_properties(hsc_compile_arithmetic
    PROPERTIES FIXTURES_SETUP arithmetic_executable
  )
  set_tests_properties(hsc_run_arithmetic
    PROPERTIES FIXTURES_REQUIRED arithmetic_executable
  )
  set_tests_properties(hsc_compile_while
    PROPERTIES FIXTURES_SETUP while_executable
  )
  set_tests_properties(hsc_run_while
    PROPERTIES FIXTURES_REQUIRED while_executable
  )
  set_tests_properties(hsc_compile_compound_assignment
    PROPERTIES FIXTURES_SETUP compound_assignment_executable
  )
  set_tests_properties(hsc_run_compound_assignment
    PROPERTIES FIXTURES_REQUIRED compound_assignment_executable
  )
  set_tests_properties(hsc_compile_functions
    PROPERTIES FIXTURES_SETUP functions_executable
  )
  set_tests_properties(hsc_run_functions
    PROPERTIES FIXTURES_REQUIRED functions_executable
  )
  set_tests_properties(hsc_compile_multifile
    PROPERTIES FIXTURES_SETUP multifile_executable
  )
  set_tests_properties(hsc_run_multifile
    PROPERTIES FIXTURES_REQUIRED multifile_executable
  )
  set_tests_properties(hsc_compile_multifile_reverse
    PROPERTIES FIXTURES_SETUP multifile_reverse_executable
  )
  set_tests_properties(hsc_run_multifile_reverse
    PROPERTIES FIXTURES_REQUIRED multifile_reverse_executable
  )
  set_tests_properties(hsc_compile_control_h
    PROPERTIES FIXTURES_SETUP control_h_executable
  )
  set_tests_properties(hsc_run_control_h
    PROPERTIES FIXTURES_REQUIRED control_h_executable
  )
  set_tests_properties(hsc_compile_structs
    PROPERTIES FIXTURES_SETUP structs_executable
  )
  set_tests_properties(hsc_run_structs
    PROPERTIES FIXTURES_REQUIRED structs_executable
  )
  set_tests_properties(hsc_compile_checked_oob
    PROPERTIES FIXTURES_SETUP checked_oob_executable
  )
  set_tests_properties(hsc_run_checked_oob
    PROPERTIES
      FIXTURES_REQUIRED checked_oob_executable
      WILL_FAIL TRUE
  )
  set_tests_properties(hsc_compile_checked_double_free
    PROPERTIES FIXTURES_SETUP checked_double_free_executable
  )
  set_tests_properties(hsc_run_checked_double_free
    PROPERTIES
      FIXTURES_REQUIRED checked_double_free_executable
      WILL_FAIL TRUE
  )
  set_tests_properties(hsc_compile_checked_use_after_free
    PROPERTIES FIXTURES_SETUP checked_use_after_free_executable
  )
  set_tests_properties(hsc_run_checked_use_after_free
    PROPERTIES
      FIXTURES_REQUIRED checked_use_after_free_executable
      WILL_FAIL TRUE
  )
  set_tests_properties(hsc_compile_preprocessor
    PROPERTIES FIXTURES_SETUP preprocessor_executable
  )
  set_tests_properties(hsc_run_preprocessor
    PROPERTIES FIXTURES_REQUIRED preprocessor_executable
  )
  set_tests_properties(hsc_compile_stdlib_core
    PROPERTIES FIXTURES_SETUP stdlib_core_executable
  )
  set_tests_properties(hsc_run_stdlib_core
    PROPERTIES FIXTURES_REQUIRED stdlib_core_executable
  )
  set_tests_properties(hsc_compile_example_stdlib_core
    PROPERTIES FIXTURES_SETUP example_stdlib_core_executable
  )
  set_tests_properties(hsc_run_example_stdlib_core
    PROPERTIES FIXTURES_REQUIRED example_stdlib_core_executable
  )
  set_tests_properties(hsc_compile_comprehensive_project_unchecked
    PROPERTIES FIXTURES_SETUP comprehensive_project_unchecked_executable
  )
  set_tests_properties(hsc_run_comprehensive_project_unchecked
    PROPERTIES FIXTURES_REQUIRED comprehensive_project_unchecked_executable
  )
  set_tests_properties(hsc_compile_comprehensive_project_static_checked
    PROPERTIES FIXTURES_SETUP comprehensive_project_static_checked_executable
  )
  set_tests_properties(hsc_run_comprehensive_project_static_checked
    PROPERTIES FIXTURES_REQUIRED comprehensive_project_static_checked_executable
  )
  set_tests_properties(hsc_compile_comprehensive_project_checked
    PROPERTIES FIXTURES_SETUP comprehensive_project_checked_executable
  )
  set_tests_properties(hsc_run_comprehensive_project_checked
    PROPERTIES FIXTURES_REQUIRED comprehensive_project_checked_executable
  )
  set_tests_properties(hsc_compile_multi_return
    PROPERTIES FIXTURES_SETUP multi_return_executable
  )
  set_tests_properties(hsc_run_multi_return
    PROPERTIES FIXTURES_REQUIRED multi_return_executable
  )
  set_tests_properties(hsc_compile_static_global
    PROPERTIES FIXTURES_SETUP static_global_executable
  )
  set_tests_properties(hsc_run_static_global
    PROPERTIES FIXTURES_REQUIRED static_global_executable
  )
  set_tests_properties(hsc_compile_pointer_slice
    PROPERTIES FIXTURES_SETUP pointer_slice_executable
  )
  set_tests_properties(hsc_run_pointer_slice
    PROPERTIES FIXTURES_REQUIRED pointer_slice_executable
  )
  set_tests_properties(hsc_compile_struct_member_offsets
    PROPERTIES FIXTURES_SETUP struct_member_offsets_executable
  )
  set_tests_properties(hsc_run_struct_member_offsets
    PROPERTIES FIXTURES_REQUIRED struct_member_offsets_executable
  )
  set_tests_properties(hsc_compile_member_template_override
    PROPERTIES FIXTURES_SETUP member_template_override_executable
  )
  set_tests_properties(hsc_run_member_template_override
    PROPERTIES FIXTURES_REQUIRED member_template_override_executable
  )
  set_tests_properties(hsc_compile_template_view
    PROPERTIES FIXTURES_SETUP template_view_executable
  )
  set_tests_properties(hsc_run_template_view
    PROPERTIES FIXTURES_REQUIRED template_view_executable
  )
  set_tests_properties(hsc_compile_user_template_ops
    PROPERTIES FIXTURES_SETUP user_template_ops_executable
  )
  set_tests_properties(hsc_run_user_template_ops
    PROPERTIES FIXTURES_REQUIRED user_template_ops_executable
  )
  set_tests_properties(hsc_compile_user_template_format
    PROPERTIES FIXTURES_SETUP user_template_format_executable
  )
  set_tests_properties(hsc_run_user_template_format
    PROPERTIES FIXTURES_REQUIRED user_template_format_executable
  )
  set_tests_properties(hsc_compile_user_template_format_fprintf
    PROPERTIES FIXTURES_SETUP user_template_format_fprintf_executable
  )
  set_tests_properties(hsc_run_user_template_format_fprintf
    PROPERTIES FIXTURES_REQUIRED user_template_format_fprintf_executable
  )
  set_tests_properties(hsc_compile_user_template_format_fprintf_expr
    PROPERTIES FIXTURES_SETUP user_template_format_fprintf_expr_executable
  )
  set_tests_properties(hsc_run_user_template_format_fprintf_expr
    PROPERTIES FIXTURES_REQUIRED user_template_format_fprintf_expr_executable
  )
  set_tests_properties(hsc_compile_user_template_format_negative
    PROPERTIES FIXTURES_SETUP user_template_format_negative_executable
  )
  set_tests_properties(hsc_run_user_template_format_negative
    PROPERTIES FIXTURES_REQUIRED user_template_format_negative_executable
  )
  set_tests_properties(hsc_compile_user_template_format_printf
    PROPERTIES FIXTURES_SETUP user_template_format_printf_executable
  )
  set_tests_properties(hsc_run_user_template_format_printf
    PROPERTIES FIXTURES_REQUIRED user_template_format_printf_executable
  )
  set_tests_properties(hsc_compile_impl_methods
    PROPERTIES FIXTURES_SETUP impl_methods_executable
  )
  set_tests_properties(hsc_run_impl_methods
    PROPERTIES FIXTURES_REQUIRED impl_methods_executable
  )
  set_tests_properties(hsc_compile_impl_method_value_copy
    PROPERTIES FIXTURES_SETUP impl_method_value_copy_executable
  )
  set_tests_properties(hsc_run_impl_method_value_copy
    PROPERTIES FIXTURES_REQUIRED impl_method_value_copy_executable
  )
  set_tests_properties(hsc_compile_impl_method_void
    PROPERTIES FIXTURES_SETUP impl_method_void_executable
  )
  set_tests_properties(hsc_run_impl_method_void
    PROPERTIES FIXTURES_REQUIRED impl_method_void_executable
  )
  set_tests_properties(hsc_compile_increment_lvalues
    PROPERTIES FIXTURES_SETUP increment_lvalues_executable
  )
  set_tests_properties(hsc_run_increment_lvalues
    PROPERTIES FIXTURES_REQUIRED increment_lvalues_executable
  )
  set_tests_properties(hsc_compile_scanf_left_context
    PROPERTIES FIXTURES_SETUP scanf_left_context_executable
  )
  set_tests_properties(hsc_run_scanf_left_context
    PROPERTIES FIXTURES_REQUIRED scanf_left_context_executable
  )
  set_tests_properties(hsc_compile_scanf_dynamic_format
    PROPERTIES FIXTURES_SETUP scanf_dynamic_format_executable
  )
  set_tests_properties(hsc_run_scanf_dynamic_format
    PROPERTIES FIXTURES_REQUIRED scanf_dynamic_format_executable
  )
  set_tests_properties(hsc_compile_scanf_partial_success
    PROPERTIES FIXTURES_SETUP scanf_partial_success_executable
  )
  set_tests_properties(hsc_run_scanf_partial_success
    PROPERTIES FIXTURES_REQUIRED scanf_partial_success_executable
  )
  set_tests_properties(hsc_compile_checked_scanf_dynamic_format_error
    PROPERTIES FIXTURES_SETUP checked_scanf_dynamic_format_error_executable
  )
  set_tests_properties(hsc_run_checked_scanf_dynamic_format_error
    PROPERTIES FIXTURES_REQUIRED checked_scanf_dynamic_format_error_executable
  )
  set_tests_properties(hsc_compile_checked_printf_dynamic_float
    PROPERTIES FIXTURES_SETUP checked_printf_dynamic_float_executable
  )
  set_tests_properties(hsc_run_checked_printf_dynamic_float
    PROPERTIES FIXTURES_REQUIRED checked_printf_dynamic_float_executable
  )
  set_tests_properties(hsc_compile_checked_dynamic_float_statement
    PROPERTIES FIXTURES_SETUP checked_dynamic_float_statement_executable
  )
  set_tests_properties(hsc_run_checked_dynamic_float_statement
    PROPERTIES FIXTURES_REQUIRED checked_dynamic_float_statement_executable
  )
  set_tests_properties(hsc_compile_floating_returns
    PROPERTIES FIXTURES_SETUP floating_returns_executable
  )
  set_tests_properties(hsc_run_floating_returns
    PROPERTIES FIXTURES_REQUIRED floating_returns_executable
  )
  set_tests_properties(hsc_compile_f16_f128_fallback
    PROPERTIES FIXTURES_SETUP f16_f128_fallback_executable
  )
  set_tests_properties(hsc_run_f16_f128_fallback
    PROPERTIES FIXTURES_REQUIRED f16_f128_fallback_executable
  )
  set_tests_properties(hsc_compile_checked_internal_global_pointer
    PROPERTIES FIXTURES_SETUP checked_internal_global_pointer_executable
  )
  set_tests_properties(hsc_run_checked_internal_global_pointer
    PROPERTIES FIXTURES_REQUIRED checked_internal_global_pointer_executable
  )
  set_tests_properties(hsc_compile_checked_static_local_pointer
    PROPERTIES FIXTURES_SETUP checked_static_local_pointer_executable
  )
  set_tests_properties(hsc_run_checked_static_local_pointer
    PROPERTIES FIXTURES_REQUIRED checked_static_local_pointer_executable
  )
  set_tests_properties(hsc_compile_checked_expired_local_return
    PROPERTIES FIXTURES_SETUP checked_expired_local_return_executable
  )
  set_tests_properties(hsc_run_checked_expired_local_return
    PROPERTIES FIXTURES_REQUIRED checked_expired_local_return_executable
  )
  set_tests_properties(hsc_compile_checked_expired_local_fallthrough
    PROPERTIES FIXTURES_SETUP checked_expired_local_fallthrough_executable
  )
  set_tests_properties(hsc_run_checked_expired_local_fallthrough
    PROPERTIES FIXTURES_REQUIRED checked_expired_local_fallthrough_executable
  )
  set_tests_properties(hsc_compile_handle_io
    PROPERTIES FIXTURES_SETUP handle_io_executable
  )
  set_tests_properties(hsc_run_handle_io
    PROPERTIES FIXTURES_REQUIRED handle_io_executable
  )
  set_tests_properties(hsc_compile_c_compat_unsigned_integer
    PROPERTIES FIXTURES_SETUP c_compat_unsigned_integer_executable
  )
  set_tests_properties(hsc_run_c_compat_unsigned_integer
    PROPERTIES FIXTURES_REQUIRED c_compat_unsigned_integer_executable
  )
  set_tests_properties(hsc_compile_c_compat_integer_conversion_matrix
    PROPERTIES FIXTURES_SETUP c_compat_integer_conversion_matrix_executable
  )
  set_tests_properties(hsc_run_c_compat_integer_conversion_matrix
    PROPERTIES FIXTURES_REQUIRED c_compat_integer_conversion_matrix_executable
  )
  set_tests_properties(hsc_compile_c_compat_integer_shift_matrix
    PROPERTIES FIXTURES_SETUP c_compat_integer_shift_matrix_executable
  )
  set_tests_properties(hsc_run_c_compat_integer_shift_matrix
    PROPERTIES FIXTURES_REQUIRED c_compat_integer_shift_matrix_executable
  )
  set_tests_properties(hsc_compile_checked_pointer_slice
    PROPERTIES FIXTURES_SETUP checked_pointer_slice_executable
  )
  set_tests_properties(hsc_run_checked_pointer_slice
    PROPERTIES FIXTURES_REQUIRED checked_pointer_slice_executable
  )
  set_tests_properties(hsc_compile_checked_scanf_left_context
    PROPERTIES FIXTURES_SETUP checked_scanf_left_context_executable
  )
  set_tests_properties(hsc_run_checked_scanf_left_context
    PROPERTIES FIXTURES_REQUIRED checked_scanf_left_context_executable
  )
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(HITSIMPLE_DEBIAN_ARCHITECTURE amd64)
  set(HITSIMPLE_RPM_ARCHITECTURE x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(HITSIMPLE_DEBIAN_ARCHITECTURE arm64)
  set(HITSIMPLE_RPM_ARCHITECTURE aarch64)
else()
  set(HITSIMPLE_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
  set(HITSIMPLE_RPM_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()

if(HITSIMPLE_RPM_RELEASE STREQUAL "")
  message(FATAL_ERROR "HITSIMPLE_RPM_RELEASE must not be empty")
endif()

set(CPACK_PACKAGE_NAME hitsimple)
set(CPACK_PACKAGE_VENDOR HitSimple)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "caokai674@gmail.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "caokai674@gmail.com")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${HITSIMPLE_DEBIAN_ARCHITECTURE})
set(CPACK_DEBIAN_FILE_NAME
  "hitsimple_${PROJECT_VERSION}_${HITSIMPLE_DEBIAN_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "clang-18 | clang (>= 1:18)")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "HitSimple experimental language compiler")
set(CPACK_RPM_PACKAGE_ARCHITECTURE ${HITSIMPLE_RPM_ARCHITECTURE})
set(CPACK_RPM_PACKAGE_RELEASE ${HITSIMPLE_RPM_RELEASE})
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_REQUIRES "clang >= 18")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_PACKAGE_LICENSE "UNLICENSED")
set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
set(CPACK_GENERATOR "DEB;RPM")
include(CPack)
