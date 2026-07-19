cmake_minimum_required(VERSION 3.24)

project(HitSimple
  VERSION 0.3.1
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
find_program(CLANGXX_EXECUTABLE NAMES clang++-18 clang++ REQUIRED)
find_program(LLVM_AR_EXECUTABLE NAMES llvm-ar-18 llvm-ar REQUIRED)
find_program(LLVM_NM_EXECUTABLE NAMES llvm-nm-18 llvm-nm REQUIRED)
find_program(LLVM_PROFDATA_EXECUTABLE NAMES llvm-profdata-18 llvm-profdata REQUIRED)
find_program(LLVM_DWARFDUMP_EXECUTABLE
  NAMES llvm-dwarfdump llvm-dwarfdump-19 llvm-dwarfdump-18
)
find_program(GDB_EXECUTABLE gdb)

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
  AsmParser
  Linker
  Support
  Target
  TargetParser
  MC
  AsmPrinter
  DebugInfoDWARF
  native
)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Found Bison ${BISON_VERSION}")
message(STATUS "Found re2c: ${RE2C_EXECUTABLE}")
message(STATUS "Found clang: ${CLANG_EXECUTABLE}")
message(STATUS "Found clang++: ${CLANGXX_EXECUTABLE}")
message(STATUS "Found llvm-ar: ${LLVM_AR_EXECUTABLE}")
message(STATUS "Found llvm-profdata: ${LLVM_PROFDATA_EXECUTABLE}")

file(GLOB HITSIMPLE_HANDWRITTEN_STANDARD_HEADERS
  "${PROJECT_SOURCE_DIR}/stdlib/*.hsh"
)
if(HITSIMPLE_HANDWRITTEN_STANDARD_HEADERS)
  message(FATAL_ERROR
    "standard library headers must be generated from StandardLibraryManifest.json; found: ${HITSIMPLE_HANDWRITTEN_STANDARD_HEADERS}"
  )
endif()

add_executable(hsc_stdlib_manifest_tool
  src/stdlib/StandardLibraryManifestTool.cpp
)
target_include_directories(hsc_stdlib_manifest_tool
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(hsc_stdlib_manifest_tool
  SYSTEM PRIVATE
    ${HITSIMPLE_LLVM_INCLUDE_DIRS}
)
target_link_libraries(hsc_stdlib_manifest_tool PRIVATE ${LLVM_LIBS})
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  target_compile_options(hsc_stdlib_manifest_tool PRIVATE /W4)
else()
  target_compile_options(hsc_stdlib_manifest_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(HITSIMPLE_GENERATED_STDLIB_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/stdlib"
)
set(HITSIMPLE_GENERATED_STDLIB_CPP
  "${CMAKE_CURRENT_BINARY_DIR}/generated/StandardLibraryGenerated.cpp"
)
set(HITSIMPLE_GENERATED_STDLIB_HEADERS
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/stdlib.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/string.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/stdio.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/math.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/ctype.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/time.hsh"
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/assert.hsh"
)
set(HITSIMPLE_STDLIB_SOURCE_MODULES
  "${HITSIMPLE_GENERATED_STDLIB_DIR}/ctype.hs"
)
add_custom_command(
  OUTPUT
    ${HITSIMPLE_GENERATED_STDLIB_CPP}
    ${HITSIMPLE_GENERATED_STDLIB_HEADERS}
    ${HITSIMPLE_STDLIB_SOURCE_MODULES}
  COMMAND $<TARGET_FILE:hsc_stdlib_manifest_tool>
          --manifest ${PROJECT_SOURCE_DIR}/stdlib/StandardLibraryManifest.json
          --cpp-out ${HITSIMPLE_GENERATED_STDLIB_CPP}
          --headers-out-dir ${HITSIMPLE_GENERATED_STDLIB_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${PROJECT_SOURCE_DIR}/stdlib/ctype.hs
          ${HITSIMPLE_GENERATED_STDLIB_DIR}/ctype.hs
  DEPENDS
    hsc_stdlib_manifest_tool
    ${PROJECT_SOURCE_DIR}/stdlib/StandardLibraryManifest.json
    ${PROJECT_SOURCE_DIR}/stdlib/ctype.hs
  VERBATIM
)
add_custom_target(hitsimple_stdlib_generated
  DEPENDS
    ${HITSIMPLE_GENERATED_STDLIB_CPP}
    ${HITSIMPLE_GENERATED_STDLIB_HEADERS}
    ${HITSIMPLE_STDLIB_SOURCE_MODULES}
)

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
  src/stdlib/StandardLibraryModules.cpp
  ${HITSIMPLE_GENERATED_STDLIB_CPP}
  src/support/Cargo.cpp
  src/support/CompilationMetrics.cpp
  src/support/Path.cpp
  src/support/Process.cpp
  src/support/ResourcePaths.cpp
  src/support/Toolchain.cpp
)
add_dependencies(hitsimple_frontend hitsimple_stdlib_generated)

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
    HITSIMPLE_BUILT_STDLIB_DIR="${HITSIMPLE_GENERATED_STDLIB_DIR}"
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
install(FILES ${HITSIMPLE_GENERATED_STDLIB_HEADERS}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/hitsimple/stdlib
)
install(FILES
  ${PROJECT_SOURCE_DIR}/stdlib/ctype.hs
  DESTINATION ${CMAKE_INSTALL_DATADIR}/hitsimple/stdlib
)
install(FILES LICENSE README.md USAGE.md Standard.md
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
  if(APPLE)
    set(HITSIMPLE_LIBRARY_SEARCH_LINK_ARGUMENT "-Wl,-undefined,error")
  else()
    set(HITSIMPLE_LIBRARY_SEARCH_LINK_ARGUMENT "-Wl,-z,defs")
  endif()

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
    tests/support/CompilationMetricsTests.cpp
    tests/support/ResourcePathsTests.cpp
    tests/stdlib/StandardLibraryTests.cpp
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
  add_hsc_shell_test(hsc_target_info_documents_uncaught_throw
    "$<TARGET_FILE:hsc> --target-info | grep -Fx 'throw.uncaught: exits with status 1 through libc exit; no diagnostic is emitted and host libc performs its ordinary stream cleanup'"
  )
  add_hsc_shell_test(hsc_target_info_documents_native_interop
    "$<TARGET_FILE:hsc> --target-info | grep -Fq 'build.native_interop: Linux x86_64/aarch64' && $<TARGET_FILE:hsc> --target-info | grep -Fq 'build.cargo: Linux x86_64/aarch64'"
  )
  if(WIN32 OR APPLE)
    set(HITSIMPLE_EXPECTED_F128_BACKEND
      "boost.cpp_bin_float.113-bit-software-binary128")
  else()
    set(HITSIMPLE_EXPECTED_F128_BACKEND "native.fp128")
  endif()
  add_hsc_shell_test(hsc_target_info_documents_checked_and_calloc_status
    "$<TARGET_FILE:hsc> --target-info > target-info.out && grep -Fq 'runtime.kind: static-library' target-info.out && grep -Fq 'f128.backend: ${HITSIMPLE_EXPECTED_F128_BACKEND}' target-info.out && grep -Fq 'stdlib.headers: stdlib.hsh, string.hsh, stdio.hsh, math.hsh, ctype.hsh, time.hsh, and assert.hsh; each standard function requires its owning system header' target-info.out && grep -Fq 'stdlib.provider.selection: optimized is the default; reference uses a manifest-declared reference implementation when available and otherwise keeps the default provider' target-info.out && grep -Fq 'stdlib.provider.dispatch: provider selection is compile-time and does not change the public View contract or safety mode' target-info.out && grep -Fq 'checked.address_coverage: local objects are removed when their function frame exits; internal globals/statics persist; extern globals, FFI raw addresses, and file handles are not registered' target-info.out && grep -Fq 'checked.file_handle_coverage: null handles are rejected by checked file I/O; handle ownership and open/closed state are not tracked' target-info.out && grep -Fq 'stdlib.formatting: native printf, print, fprintf, scanf, and fscanf use runtime descriptors; C compatibility does not accept variadic declarations' target-info.out && grep -Fq 'stdlib.raw_output: print(view as none), put, and fput write the complete View and return the written byte count' target-info.out && grep -Fq 'float.fallback: f16 math evaluates as f32 and rounds back to binary16; f128 uses the target-specific runtime backend' target-info.out && grep -Fq 'abi.c_compat.aggregate_by_value: only x86_64 SysV ELF targets; other targets reject C struct by-value parameters and returns; supported direct aggregates up to 16 bytes use at most two INTEGER/SSE eightbytes' target-info.out && grep -Fq 'abi.c_compat.aggregate_indirect: on supported x86_64 SysV ELF targets, aggregates larger than 16 bytes use sret returns and byval parameters' target-info.out && grep -Fq 'abi.c_compat.aggregate_limits:' target-info.out && grep -Fq 'C compatibility validates external ABI shapes before link' target-info.out"
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
  add_hsc_shell_test(hsc_emits_target_object
    "$<TARGET_FILE:hsc> --emit-object ${PROJECT_SOURCE_DIR}/examples/hello.hs -o hello.o && test -s hello.o"
  )
  add_hsc_shell_test(hsc_optimization_levels_build_all_artifact_types
    "for opt in -O0 -O1 -O2 -O3 -Os; do $<TARGET_FILE:hsc> --clang ${CLANG_EXECUTABLE} $opt ${PROJECT_SOURCE_DIR}/examples/hello.hs -o executable-$opt && ./executable-$opt && $<TARGET_FILE:hsc> --clang ${CLANG_EXECUTABLE} --emit-object $opt ${PROJECT_SOURCE_DIR}/examples/hello.hs -o object-$opt.o && test -s object-$opt.o && $<TARGET_FILE:hsc> --clang ${CLANG_EXECUTABLE} --crate-type=staticlib $opt ${PROJECT_SOURCE_DIR}/examples/hello.hs -o staticlib-$opt.a && test -s staticlib-$opt.a; done"
  )
  add_hsc_shell_test(hsc_optimization_is_forwarded_to_all_clang_build_steps
    "printf '%s\n' '#!/bin/sh' 'printf \"%s\\n\" \"$@\" >> clang.args' 'exec ${CLANG_EXECUTABLE} \"$@\"' > clang-wrapper && printf '%s\n' '#!/bin/sh' 'printf \"%s\\n\" \"$@\" >> clang.args' 'exec ${CLANGXX_EXECUTABLE} \"$@\"' > clangxx-wrapper && chmod +x clang-wrapper clangxx-wrapper && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper ${PROJECT_SOURCE_DIR}/examples/hello.hs -o default-program && grep -Fx -- '-O2' clang.args && ! grep -Eq -- '^-O(0|1|3|s)$' clang.args && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper -O0 -O3 ${PROJECT_SOURCE_DIR}/examples/hello.hs -o last-wins-program && grep -Fx -- '-O3' clang.args && ! grep -Fx -- '-O0' clang.args && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper --emit-object -O1 ${PROJECT_SOURCE_DIR}/examples/hello.hs -o object.o && grep -Fx -- '-O1' clang.args && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper --crate-type=staticlib -Os ${PROJECT_SOURCE_DIR}/examples/hello.hs -o library.a && grep -Fx -- '-Os' clang.args && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper -Os ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs --c-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import_helper.c -o mixed-c && ./mixed-c && grep -E '^-O(0|1|2|3|s)$' clang.args > mixed-c.optimizations && test \"$(wc -l < mixed-c.optimizations)\" -eq 3 && test \"$(sort -u mixed-c.optimizations | wc -l)\" -eq 1 && grep -Fx -- '-Os' mixed-c.optimizations && rm -f clang.args && $<TARGET_FILE:hsc> --clang $PWD/clang-wrapper --clangxx $PWD/clangxx-wrapper -Os ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --cxx-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp -o mixed-cxx && ./mixed-cxx && grep -E '^-O(0|1|2|3|s)$' clang.args > mixed-cxx.optimizations && test \"$(wc -l < mixed-cxx.optimizations)\" -eq 3 && test \"$(sort -u mixed-cxx.optimizations | wc -l)\" -eq 1 && grep -Fx -- '-Os' mixed-cxx.optimizations"
  )
  add_hsc_shell_test(hsc_emit_llvm_ignores_native_optimization_selection
    "$<TARGET_FILE:hsc> --emit-llvm -O0 ${PROJECT_SOURCE_DIR}/examples/hello.hs -o o0.ll && $<TARGET_FILE:hsc> --emit-llvm -O3 ${PROJECT_SOURCE_DIR}/examples/hello.hs -o o3.ll && cmp -s o0.ll o3.ll"
  )
  add_hsc_shell_test(hsc_pgo_instrument_and_use
    "$<TARGET_FILE:hsc> -O2 --pgo-instrument=program.profraw ${PROJECT_SOURCE_DIR}/examples/hello.hs -o program-instrumented && ./program-instrumented >instrumented.stdout && grep -Fx '42' instrumented.stdout && test -s program.profraw && ${LLVM_PROFDATA_EXECUTABLE} merge -sparse program.profraw -o program.profdata && test -s program.profdata && $<TARGET_FILE:hsc> -O2 --pgo-use=program.profdata ${PROJECT_SOURCE_DIR}/examples/hello.hs -o program-pgo && ./program-pgo >pgo.stdout && grep -Fx '42' pgo.stdout"
  )
  add_hsc_shell_test(hsc_pgo_reports_invalid_options_and_outputs
    "! $<TARGET_FILE:hsc> --pgo-unknown ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq \"unknown option '--pgo-unknown'\" stderr && ! $<TARGET_FILE:hsc> --pgo-instrument=program.profraw --pgo-use=program.profdata ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'mutually exclusive' stderr && ! $<TARGET_FILE:hsc> --pgo-use=missing.profdata ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'PGO profile data does not exist' stderr && mkdir -p profile-directory && ! $<TARGET_FILE:hsc> --pgo-use=profile-directory ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'PGO profile data is not a regular file' stderr && ! $<TARGET_FILE:hsc> --pgo-instrument=program.profraw --emit-object ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'only supported for --crate-type=bin' stderr && ! $<TARGET_FILE:hsc> --pgo-instrument=program.profraw --crate-type=staticlib ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'only supported for --crate-type=bin' stderr && ! $<TARGET_FILE:hsc> --pgo-instrument=program.profraw --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq 'not supported with --emit-llvm' stderr"
  )
  add_hsc_shell_test(hsc_help_documents_optimization_and_pgo
    "$<TARGET_FILE:hsc> --help > help.txt && grep -Fq -- '-O0, -O1, -O2, -O3, -Os' help.txt && grep -Fq 'default: -O2' help.txt && grep -Fq -- '--pgo-instrument=<profraw>' help.txt && grep -Fq -- '--pgo-use=<profdata>' help.txt && grep -Fq 'llvm-profdata merge -sparse' help.txt"
  )
  add_hsc_shell_test(hsc_ctype_reference_executable
    "$<TARGET_FILE:hsc> --stdlib-provider=reference ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o program && ./program"
  )
  add_hsc_shell_test(hsc_ctype_optimized_matches_reference
    "$<TARGET_FILE:hsc> --stdlib-provider=optimized ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o program && ./program"
  )
  add_hsc_shell_test(hsc_ctype_provider_modes_match
    "for mode in --unchecked --static-checked --checked; do for provider in optimized reference; do $<TARGET_FILE:hsc> --stdlib-provider=$provider $mode ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o program-$provider-$mode && ./program-$provider-$mode; done; done"
  )
  add_hsc_shell_test(hsc_ctype_reference_emit_llvm_injects_module
    "$<TARGET_FILE:hsc> --stdlib-provider=reference --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.ll && grep -Fq 'define i8 @__hs_stdlib_ctype_is_digit(i32' module.ll && grep -Fq 'call i8 @__hs_stdlib_ctype_is_digit(i32' module.ll"
  )
  add_hsc_shell_test(hsc_ctype_optimized_emit_llvm_omits_module
    "$<TARGET_FILE:hsc> --stdlib-provider=optimized --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.ll && ! grep -Fq '__hs_stdlib_ctype_is_digit' module.ll"
  )
  add_hsc_shell_test(hsc_ctype_reference_object_includes_module
    "$<TARGET_FILE:hsc> --stdlib-provider=reference --emit-object ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.o && ${LLVM_NM_EXECUTABLE} module.o | grep -Eq ' T _?__hs_stdlib_ctype_is_digit'"
  )
  add_hsc_shell_test(hsc_ctype_reference_object_timing_includes_module
    "$<TARGET_FILE:hsc> --stdlib-provider=reference --timing-json=timing.json --emit-object ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.o && ${CMAKE_COMMAND} -DJSON=$PWD/timing.json -DEXPECTED_OUTCOME=success -DEXPECTED_TU_COUNT=2 -DEXPECTED_C_COMPAT_LOWERING_STATUS=skipped -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_ctype_reference_staticlib_includes_module_once
    "$<TARGET_FILE:hsc> --stdlib-provider=reference --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_main.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_lib.hs -o libctype.a && test \"$(${LLVM_NM_EXECUTABLE} libctype.a | grep -Ec ' T _?__hs_stdlib_ctype_is_digit$')\" = 1"
  )
  add_hsc_shell_test(hsc_ctype_reference_multifile_deduplicates_module
    "$<TARGET_FILE:hsc> --stdlib-provider=reference ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_main.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_lib.hs -o program && ./program"
  )
  add_hsc_shell_test(hsc_installed_ctype_reference_all_artifacts
    "${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix $PWD/install && $PWD/install/bin/hsc --stdlib-provider=reference ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o program && ./program && $PWD/install/bin/hsc --stdlib-provider=reference --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.ll && grep -Fq 'define i8 @__hs_stdlib_ctype_is_digit(i32' module.ll && $PWD/install/bin/hsc --stdlib-provider=reference --emit-object ${PROJECT_SOURCE_DIR}/tests/cases/run/ctype_reference.hs -o module.o && ${LLVM_NM_EXECUTABLE} module.o | grep -Eq ' T _?__hs_stdlib_ctype_is_digit' && $PWD/install/bin/hsc --stdlib-provider=reference --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_main.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/ctype_reference_lib.hs -o libctype.a && test \"$(${LLVM_NM_EXECUTABLE} libctype.a | grep -Ec ' T _?__hs_stdlib_ctype_is_digit$')\" = 1"
  )
  add_hsc_shell_test(hsc_rejects_user_stdlib_implementation_symbol
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/sema/reserved_stdlib_implementation.hs -o program >stdout 2>stderr && grep -Fq 'reserved standard library implementation symbol' stderr"
  )
  add_hsc_shell_test(hsc_c_abi_object_native_to_hsc
    "$<TARGET_FILE:hsc> --emit-object ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs -o hsc_export.o && ${CLANG_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export_caller.c hsc_export.o $<TARGET_FILE:hitsimple_runtime> -lm -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_abi_object_hsc_to_native
    "$<TARGET_FILE:hsc> --crate-type=object ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs -o hsc_import.o && ${CLANG_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import_helper.c hsc_import.o $<TARGET_FILE:hitsimple_runtime> -lm -o program && ./program"
  )
  add_hsc_shell_test(hsc_c_abi_staticlib_native_to_hsc
    "$<TARGET_FILE:hsc> --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs -o libhsc.a && ${LLVM_AR_EXECUTABLE} t libhsc.a | grep -Fq 'hitsimple_runtime' && ${CLANG_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export_caller.c libhsc.a -lm -o program && ./program"
  )
  add_hsc_shell_test(hsc_rejects_multifile_object_output
    "! $<TARGET_FILE:hsc> --emit-object ${PROJECT_SOURCE_DIR}/examples/hello.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/arithmetic.hs -o program.o >stdout 2>stderr && grep -Fq -- '--crate-type=object supports exactly one input file' stderr"
  )
  add_hsc_shell_test(hsc_rejects_object_with_llvm_ir_action
    "! $<TARGET_FILE:hsc> --emit-object --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs >stdout 2>stderr && grep -Fq -- '--crate-type is not supported with --emit-llvm' stderr"
  )
  add_hsc_shell_test(hsc_reports_missing_llvm_ar
    "! env -u HITSIMPLE_LLVM_AR PATH= $<TARGET_FILE:hsc> --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs -o libhsc.a >stdout 2>stderr && grep -Fq 'no llvm-ar tool found' stderr"
  )
  add_hsc_shell_test(hsc_mixed_hsc_entry_calls_c_source
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs --c-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import_helper.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_hsc_entry_calls_cxx_wrapper
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --cxx-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_native_c_entry_calls_hsc
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs --entry=native --c-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export_caller.c -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_native_cxx_entry_calls_hsc
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs --entry=native --cxx-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_hsc_export_caller.cpp -o program && ./program"
  )
  add_hsc_shell_test(hsc_cxx_staticlib_calls_hsc
    "$<TARGET_FILE:hsc> --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs -o libhsc.a && ${CLANGXX_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_hsc_export_caller.cpp libhsc.a -lm -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_cxx_archive_link_input
    "${CLANGXX_EXECUTABLE} -c ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp -o cpp_wrapper.o && ${LLVM_AR_EXECUTABLE} rcs libcpp_wrapper.a cpp_wrapper.o && $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --linker-language=cxx --link-input libcpp_wrapper.a -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_library_search_and_link_argument
    "mkdir -p native-lib && ${CLANGXX_EXECUTABLE} -c ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp -o native-lib/cpp_wrapper.o && ${LLVM_AR_EXECUTABLE} rcs native-lib/libcpp_wrapper.a native-lib/cpp_wrapper.o && $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --linker-language=cxx -L native-lib -l cpp_wrapper --link-arg ${HITSIMPLE_LIBRARY_SEARCH_LINK_ARGUMENT} -o program && ./program"
  )
  add_hsc_shell_test(hsc_mixed_rejects_archive_input_without_linker_language
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --link-input missing.o >stdout 2>stderr && grep -Fq -- '--linker-language=c|cxx' stderr"
  )
  add_hsc_shell_test(hsc_mixed_reports_missing_clangxx
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --cxx-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp --clangxx ${CMAKE_CURRENT_BINARY_DIR}/missing-clangxx -o program >stdout 2>stderr && grep -Fq 'configured Clang executable was not found' stderr"
  )
  add_hsc_shell_test(hsc_mixed_forwards_c_compiler_failure
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs --c-source missing.c -o program >stdout 2>stderr && grep -Fq 'C compiler failed for' stderr && grep -Fq 'no such file' stderr"
  )
  add_hsc_shell_test(hsc_mixed_forwards_linker_failure
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/examples/hello.hs --linker-language=c --link-input missing.o -o program >stdout 2>stderr && grep -Fq 'linker failed while linking executable' stderr && grep -Fq 'missing.o' stderr"
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_hsc_shell_test(hsc_mixed_wrong_linker_language_fails
      "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_cpp.hs --cxx-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/cpp_wrapper.cpp --linker-language=c -o program >stdout 2>stderr && grep -Fq 'linker failed while linking executable' stderr"
    )
  endif()
  add_hsc_shell_test(hsc_mixed_rejects_native_entry_with_hsc_main
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs --entry=native --c-source ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import_helper.c -o program >stdout 2>stderr && grep -Fq -- '--entry=native forbids a HitSimple main function' stderr"
  )
  add_hsc_shell_test(hsc_mixed_rejects_external_inputs_for_object_output
    "! $<TARGET_FILE:hsc> --emit-object ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_import.hs --c-source missing.c -o program.o >stdout 2>stderr && grep -Fq 'native linker options are not supported with --crate-type' stderr"
  )
  add_hsc_shell_test(hsc_cargo_staticlib_hsc_calls_rust
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_staticlib/Cargo.toml --cargo-profile release --cargo-features bonus -o program && ./program"
  )
  add_hsc_shell_test(hsc_cargo_virtual_workspace_package
    "$<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_virtual/Cargo.toml --cargo-package hitsimple-rust-virtual-staticlib -o program && ./program"
  )
  add_hsc_shell_test(hsc_cargo_forwards_native_link_requirements
    "mkdir -p cargo-native && ${CLANG_EXECUTABLE} -c ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_native_link/rust_native_helper.c -o cargo-native/rust_native_helper.o && ${LLVM_AR_EXECUTABLE} rcs cargo-native/librust_native_helper.a cargo-native/rust_native_helper.o && HITSIMPLE_RUST_NATIVE_LIB_DIR=$PWD/cargo-native $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_native_link/Cargo.toml -o program && ./program"
  )
  add_hsc_shell_test(hsc_cargo_reports_unresolved_native_requirement
    "mkdir -p cargo-native-missing && ! HITSIMPLE_RUST_NATIVE_LIB_DIR=$PWD/cargo-native-missing $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_native_link/Cargo.toml -o program >stdout 2>stderr && grep -Fq 'could not find native static library' stderr && grep -Fq 'Cargo build failed' stderr"
  )
  add_hsc_shell_test(hsc_cargo_rust_calls_hsc_staticlib
    "$<TARGET_FILE:hsc> --crate-type=staticlib ${PROJECT_SOURCE_DIR}/tests/cases/interop/c_abi_export.hs -o libhsc.a && HITSIMPLE_HSC_LIB_DIR=$PWD cargo test --manifest-path ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_hsc_caller/Cargo.toml --quiet"
  )
  add_hsc_shell_test(hsc_cargo_reports_missing_tool
    "! env -u HITSIMPLE_CARGO PATH= $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_staticlib/Cargo.toml -o program >stdout 2>stderr && grep -Fq 'no Cargo tool found' stderr"
  )
  add_hsc_shell_test(hsc_cargo_requires_virtual_workspace_package
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_virtual/Cargo.toml -o program >stdout 2>stderr && grep -Fq 'virtual workspace' stderr"
  )
  add_hsc_shell_test(hsc_cargo_rejects_nonstaticlib_target
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_nonstatic/Cargo.toml -o program >stdout 2>stderr && grep -Fq 'did not emit a staticlib artifact' stderr"
  )
  add_hsc_shell_test(hsc_cargo_forwards_build_failure
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/tests/cases/interop/hsc_calls_rust.hs --cargo-manifest ${PROJECT_SOURCE_DIR}/tests/cases/interop/rust_staticlib/Cargo.toml --cargo-features missing -o program >stdout 2>stderr && grep -Fq 'Cargo build failed' stderr && grep -Fq 'does not contain this feature' stderr"
  )
  add_hsc_shell_test(hsc_cargo_options_require_manifest
    "! $<TARGET_FILE:hsc> ${PROJECT_SOURCE_DIR}/examples/hello.hs --cargo-profile release -o program >stdout 2>stderr && grep -Fq 'Cargo options require --cargo-manifest' stderr"
  )
  set_tests_properties(
    hsc_cargo_staticlib_hsc_calls_rust
    hsc_cargo_virtual_workspace_package
    hsc_cargo_forwards_native_link_requirements
    hsc_cargo_reports_unresolved_native_requirement
    hsc_cargo_rust_calls_hsc_staticlib
    hsc_cargo_reports_missing_tool
    hsc_cargo_requires_virtual_workspace_package
    hsc_cargo_rejects_nonstaticlib_target
    hsc_cargo_forwards_build_failure
    hsc_cargo_options_require_manifest
    PROPERTIES ENVIRONMENT "RUSTC_WRAPPER=;RUSTC_WORKSPACE_WRAPPER=")
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
  add_hsc_shell_test(hsc_checked_abs_minimum_static_diagnostic
    "! $<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/sema/checked_abs_minimum_static.hs -o program >stdout 2>stderr && grep -Fq 'static safety check failed: abs of minimum signed value' stderr"
  )
  add_hsc_shell_test(hsc_static_checked_calloc_overflow_diagnostic
    "! $<TARGET_FILE:hsc> --static-checked ${PROJECT_SOURCE_DIR}/tests/cases/sema/checked_calloc_overflow.hs -o program >stdout 2>stderr && grep -Fq 'static safety check failed: calloc size overflow' stderr"
  )
  add_hsc_shell_test(hsc_static_checked_memcpy_overlap_diagnostic
    "! $<TARGET_FILE:hsc> --static-checked ${PROJECT_SOURCE_DIR}/tests/cases/sema/checked_memcpy_overlap.hs -o program >stdout 2>stderr && grep -Fq 'static safety check failed: overlapping memcpy ranges' stderr"
  )
  add_hsc_shell_test(hsc_static_checked_fread_bounds_diagnostic
    "! $<TARGET_FILE:hsc> --static-checked ${PROJECT_SOURCE_DIR}/tests/cases/sema/checked_fread_bounds.hs -o program >stdout 2>stderr && grep -Fq 'static safety check failed: fread destination range out of bounds' stderr"
  )
  add_hsc_shell_test(hsc_static_checked_fwrite_bounds_diagnostic
    "! $<TARGET_FILE:hsc> --static-checked ${PROJECT_SOURCE_DIR}/tests/cases/sema/checked_fwrite_bounds.hs -o program >stdout 2>stderr && grep -Fq 'static safety check failed: fwrite source range out of bounds' stderr"
  )
  add_hsc_shell_test(hsc_checked_abs_minimum_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_abs_minimum.hs -o program && ! printf '%s\\n' '-128' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: abs of minimum signed value' stderr"
  )
  add_hsc_shell_test(hsc_static_checked_abs_minimum_omits_runtime_check
    "$<TARGET_FILE:hsc> --static-checked --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_abs_minimum.hs -o module.ll && ! grep -Fq '@hs_abs_overflow' module.ll"
  )
  add_hsc_shell_test(hsc_checked_unterminated_fopen_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_unterminated_fopen.hs -o program && ! ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: unterminated checked file name' stderr"
  )
  add_hsc_shell_test(hsc_checked_null_file_handle_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_null_file_handle.hs -o program && ! ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid file handle' stderr"
  )
  add_hsc_shell_test(hsc_checked_calloc_overflow_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_calloc_overflow_runtime.hs -o program && ! printf '%s\\n' '18446744073709551615' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: calloc size overflow' stderr"
  )
  add_hsc_shell_test(hsc_checked_fread_overflow_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_fread_overflow_runtime.hs -o program && ! printf '%s\\n' '18446744073709551615' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: fread size overflow' stderr"
  )
  add_hsc_shell_test(hsc_checked_fwrite_overflow_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_fwrite_overflow_runtime.hs -o program && ! printf '%s\\n' '18446744073709551615' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: fwrite size overflow' stderr"
  )
  add_hsc_shell_test(hsc_checked_fread_bounds_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_fread_bounds.hs -o program && ! printf '2' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid fread destination range' stderr"
  )
  add_hsc_shell_test(hsc_checked_fwrite_bounds_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_fwrite_bounds.hs -o program && ! printf '2' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid fwrite source range' stderr"
  )
  add_hsc_shell_test(hsc_checked_unterminated_strlen_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_unterminated_strlen.hs -o program && ! ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: unterminated checked string' stderr"
  )
  add_hsc_shell_test(hsc_checked_strcpy_capacity_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_strcpy_capacity.hs -o program && ! ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: insufficient strcpy destination capacity' stderr"
  )
  add_hsc_shell_test(hsc_checked_strcat_capacity_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_strcat_capacity.hs -o program && ! ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: insufficient strcat destination capacity' stderr"
  )
  add_hsc_shell_test(hsc_checked_memcpy_bounds_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_memcpy_bounds.hs -o program && ! printf '2' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: invalid memcpy destination range' stderr"
  )
  add_hsc_shell_test(hsc_checked_memcpy_overlap_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_memcpy_overlap.hs -o program && ! printf '1' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: overlapping memcpy ranges' stderr"
  )
  add_hsc_shell_test(hsc_checked_zero_length_memory_operands
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_zero_length_memory.hs -o program && ./program"
  )
  add_hsc_shell_test(hsc_checked_dynamic_scan_target_runtime_error
    "$<TARGET_FILE:hsc> --checked ${PROJECT_SOURCE_DIR}/tests/cases/run/checked_dynamic_scan_target.hs -o program && ! printf 'x\\n' | ./program >stdout 2>stderr && grep -Fq 'hitsimple runtime error: scan string target must leave room for a NUL terminator' stderr"
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
  add_test(NAME hsc_compile_f128_format_scan
    COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/cases/run/f128_format_scan.hs
            -o ${CMAKE_CURRENT_BINARY_DIR}/f128_format_scan_smoke
  )
  add_hsc_shell_test(hsc_run_f128_format_scan
    "printf '1.5\\n' | ${CMAKE_CURRENT_BINARY_DIR}/f128_format_scan_smoke >stdout && grep -Fx '1.5' stdout"
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
  set_tests_properties(hsc_compile_f128_format_scan
    PROPERTIES FIXTURES_SETUP f128_format_scan_executable
  )
  set_tests_properties(hsc_run_f128_format_scan
    PROPERTIES FIXTURES_REQUIRED f128_format_scan_executable
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

  add_hsc_shell_test(hsc_timing_json_emit_llvm_success
    "$<TARGET_FILE:hsc> --timing-json=timing-success.json --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs -o module.ll && ${CMAKE_COMMAND} -DJSON=$PWD/timing-success.json -DEXPECTED_OUTCOME=success -DEXPECTED_TU_COUNT=1 -DEXPECTED_C_COMPAT_LOWERING_STATUS=skipped -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_timing_json_c_compat
    "$<TARGET_FILE:hsc> --timing-json=timing-c-compat.json --c-compat --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/compat/c_runtime_double.c -o module.ll && ${CMAKE_COMMAND} -DJSON=$PWD/timing-c-compat.json -DEXPECTED_OUTCOME=success -DEXPECTED_TU_COUNT=1 -DEXPECTED_C_COMPAT_LOWERING_STATUS=completed -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_timing_json_multifile
    "$<TARGET_FILE:hsc> --timing-json=timing-multifile.json ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/lib.hs ${PROJECT_SOURCE_DIR}/tests/cases/run/multifile/main.hs -o program && ${CMAKE_COMMAND} -DJSON=$PWD/timing-multifile.json -DEXPECTED_OUTCOME=success -DEXPECTED_TU_COUNT=2 -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_timing_json_sema_failure
    "! $<TARGET_FILE:hsc> --timing-json=timing-sema-failure.json --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/try_catch_integer_to_float_rejected.hs -o module.ll && ${CMAKE_COMMAND} -DJSON=$PWD/timing-sema-failure.json -DEXPECTED_OUTCOME=failure -DEXPECTED_FAILED_STAGE=sema_hir -DEXPECTED_TU_COUNT=1 -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_timing_json_link_failure
    "! $<TARGET_FILE:hsc> --timing-json=timing-link-failure.json --clang ${CMAKE_COMMAND} ${PROJECT_SOURCE_DIR}/examples/hello.hs -o program && ${CMAKE_COMMAND} -DJSON=$PWD/timing-link-failure.json -DEXPECTED_OUTCOME=failure -DEXPECTED_FAILED_STAGE=clang_backend_link -DEXPECTED_TU_COUNT=1 -P ${PROJECT_SOURCE_DIR}/cmake/VerifyTimingJson.cmake"
  )
  add_hsc_shell_test(hsc_timing_does_not_change_emit_llvm_stdout
    "$<TARGET_FILE:hsc> --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs > plain.ll && $<TARGET_FILE:hsc> --timing --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs > timed.ll 2>timing.stderr && cmp -s plain.ll timed.ll && grep -Fq 'hsc: timing outcome=success' timing.stderr"
  )
  add_hsc_shell_test(hsc_rejects_timing_json_output_conflict
    "! $<TARGET_FILE:hsc> --timing-json=module.ll --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs -o module.ll 2>stderr && grep -Fq -- '--timing-json path must not match -o output path' stderr"
  )
  add_hsc_shell_test(hsc_rejects_timing_json_default_output_conflict
    "! $<TARGET_FILE:hsc> --timing-json=a.out ${PROJECT_SOURCE_DIR}/examples/hello.hs 2>stderr && grep -Fq -- '--timing-json path must not match default output path' stderr"
  )
  add_hsc_shell_test(hsc_rejects_timing_json_missing_parent
    "! $<TARGET_FILE:hsc> --timing-json=missing-parent/timing.json --emit-llvm ${PROJECT_SOURCE_DIR}/examples/hello.hs -o module.ll 2>stderr && grep -Fq 'timing JSON directory does not exist' stderr"
  )
  add_hsc_shell_test(hsc_emit_llvm_debug_metadata
    "$<TARGET_FILE:hsc> --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/debug_info.hs -o plain.ll && ! grep -Fq '!llvm.dbg.cu' plain.ll && $<TARGET_FILE:hsc> -g --emit-llvm ${PROJECT_SOURCE_DIR}/tests/cases/run/debug_info.hs -o debug.ll && grep -Fq '!llvm.dbg.cu' debug.ll && grep -Fq '!DISubprogram' debug.ll && grep -Fq '!DILocalVariable(name: \"value\"' debug.ll && grep -Fq 'llvm.dbg.declare' debug.ll"
  )
  add_hsc_shell_test(hsc_rejects_debug_info_for_non_compile_action
    "! $<TARGET_FILE:hsc> -g --dump-hir ${PROJECT_SOURCE_DIR}/examples/hello.hs 2>stderr && grep -Fq -- '-g is only supported for executable builds and --emit-llvm' stderr"
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64|ARM64)$" AND
     LLVM_DWARFDUMP_EXECUTABLE AND GDB_EXECUTABLE)
    add_hsc_shell_test(hsc_debug_info_gdb
      "${CMAKE_COMMAND} -DHSC=$<TARGET_FILE:hsc> -DLLVM_DWARFDUMP=${LLVM_DWARFDUMP_EXECUTABLE} -DGDB=${GDB_EXECUTABLE} -DSOURCE=${PROJECT_SOURCE_DIR}/tests/cases/run/debug_info.hs -P ${PROJECT_SOURCE_DIR}/tests/support/RunGdbDebugInfoTest.cmake"
    )
    set_tests_properties(hsc_debug_info_gdb PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP: ptrace is unavailable")
  endif()
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
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
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
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
set(CPACK_GENERATOR "DEB;RPM")
include(CPack)
