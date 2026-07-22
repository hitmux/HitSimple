#include "driver/Driver.h"

#include "driver/Diagnostics.h"
#include "driver/DriverOptions.h"
#include "driver/FrontendPipeline.h"
#include "driver/NativeBuild.h"
#include "hitsimple/codegen/TargetCapabilities.h"
#include "hitsimple/support/Cargo.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"
#include "hitsimple/support/Toolchain.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/TargetParser/Triple.h>

#include <bit>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace hitsimple::driver {

void printHelp(std::ostream& out) {
  out << "Usage: hsc [options] <input>...\n"
      << "\n"
      << "Multiple input files are compiled as independent translation units and linked.\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help        Show this help message\n"
      << "  --version         Show compiler version\n"
      << "  --dump-tokens     Print lexer tokens\n"
      << "  --dump-ast        Print parser AST\n"
      << "  --dump-hir        Print semantic HIR\n"
      << "  --emit-llvm       Emit LLVM IR\n"
      << "  --emit-object     Emit one target object file\n"
      << "  --crate-type=<bin|object|staticlib>\n"
      << "                   Select executable, object, or static library output\n"
      << "  -O0, -O1, -O2, -O3, -Os\n"
      << "                   Select the embedded LLVM optimization level (default: -O2);\n"
      << "                   when repeated, the last option wins\n"
      << "  --optimization-remarks=<path>\n"
      << "                   Write opt-in HitSimple pipeline remarks\n"
      << "  --sanitize=<address|undefined>\n"
      << "                   Build a Linux test executable with the selected sanitizer\n"
      << "  -g                Emit native debug information\n"
      << "  --pgo-instrument=<profraw>\n"
      << "                   Build an instrumented executable that writes LLVM\n"
      << "                   raw profile data to <profraw>\n"
      << "  --pgo-use=<profdata>\n"
      << "                   Build an executable using an LLVM merged profile\n"
      << "  --timing          Print compilation timing to stderr\n"
      << "  --timing-json=<path>\n"
      << "                   Write versioned compilation timing JSON\n"
      << "  --diagnostic-format=json\n"
      << "                   Write compiler diagnostics as NDJSON to stderr\n"
      << "  --c-compat        Parse all inputs as C compatibility source\n"
      << "  -E, --preprocess-only\n"
      << "                   Print preprocessed source\n"
      << "  --target-info     Print implementation-defined target information\n"
      << "  --checked         Enable static and runtime safety checks\n"
      << "  --static-checked  Enable static safety diagnostics without runtime checks\n"
      << "  --unchecked       Disable safety checks (default)\n"
      << "  --clang <path>    Use a specific Clang executable for linking\n"
      << "  --clangxx <path>  Use a specific Clang++ executable\n"
      << "  --c-source <path> Compile and link a native C source file\n"
      << "  --cxx-source <path>\n"
      << "                   Compile and link a native C++ source file\n"
      << "  --link-input <path>\n"
      << "                   Link an object, archive, or shared library\n"
      << "  -L <dir>          Add a native library search directory\n"
      << "  -l <name>         Link a native library\n"
      << "  --link-arg <arg>  Pass one argument through to the native linker\n"
      << "  --entry=hsc|native\n"
      << "                   Select a HitSimple or native program entry\n"
      << "  --linker-language=c|cxx\n"
      << "                   Select the final native linker driver\n"
      << "  --cargo-manifest <Cargo.toml>\n"
      << "                   Build and link one Cargo staticlib package\n"
      << "  --cargo-package <name>\n"
      << "                   Select a package from a Cargo workspace\n"
      << "  --cargo-profile <name>\n"
      << "                   Pass a Cargo build profile\n"
      << "  --cargo-features <list>\n"
      << "                   Pass Cargo's comma- or space-separated feature list\n"
      << "  --cargo-no-default-features\n"
      << "                   Disable Cargo default features\n"
      << "  -o <path>         Write output to <path>\n"
      << "\n"
      << "PGO workflow (executable builds only):\n"
      << "  hsc -O2 --pgo-instrument=program.profraw program.hs -o program-instrumented\n"
      << "  ./program-instrumented\n"
      << "  llvm-profdata merge -sparse program.profraw -o program.profdata\n"
      << "  hsc -O2 --pgo-use=program.profdata program.hs -o program-pgo\n";
}

void printVersion(std::ostream& out) {
  out << "hsc " << HITSIMPLE_VERSION << "\n"
      << "LLVM " << HITSIMPLE_LLVM_VERSION << "\n";
}

std::string_view hostByteOrder() {
  if constexpr (std::endian::native == std::endian::little) {
    return "little";
  }
  if constexpr (std::endian::native == std::endian::big) {
    return "big";
  }
  return "mixed";
}
void printTargetInfo(std::ostream& out,
                     const hitsimple::support::ClangSelection& clang) {
  const llvm::Triple target(targetTriple());
  out << "target.triple: " << target.str() << '\n'
      << "llvm.version: " << HITSIMPLE_LLVM_VERSION << '\n'
      << "clang.path: "
      << (clang.path ? hitsimple::support::pathToUtf8(*clang.path)
                     : std::string("not found")) << '\n'
      << "clang.source: " << clang.source << '\n'
      << "runtime.kind: static-library\n"
      << "runtime.path: "
      << hitsimple::support::pathToUtf8(
             hitsimple::support::runtimeLibraryPath()) << '\n'
      << "f128.backend: "
      << (hitsimple::codegen::usesSoftwareF128Backend(target.str())
              ? "boost.cpp_bin_float.113-bit-software-binary128"
              : "native.fp128")
      << '\n'
      << "preprocessor.backend: mcpp vendored\n"
      << "pointer.length: " << sizeof(void*) << '\n'
      << "byte.order: " << hostByteOrder() << '\n'
      << "safety.modes: unchecked, static-checked, checked\n"
      << "checked.runtime: dynamic allocation registry plus compiler-generated "
         "local frame and internal global/static registrations for pointer "
         "load/store checks\n"
      << "checked.address_coverage: local objects are removed when their function "
         "frame exits; internal globals/statics persist; extern globals, FFI "
         "raw addresses, and file handles are not registered\n"
      << "checked.file_handle_coverage: null handles are rejected by checked "
         "file I/O; handle ownership and open/closed state are not tracked\n"
      << "static-checked.runtime-overhead: none\n"
      << "throw.uncaught: exits with status 1 through libc exit; no diagnostic "
         "is emitted and host libc performs its ordinary stream cleanup\n"
      << "abi.symbol.names: source identifiers use external linkage as written\n"
      << "abi.main: unannotated main uses i32, fallthrough returns 0\n"
      << "build.translation_units: each input is preprocessed, parsed, analyzed, "
         "and emitted independently; LLVM modules and runtime link once\n"
      << "build.cross_tu: matching extern declarations are required; C "
         "compatibility validates external ABI shapes before link; typedefs, "
         "macro expansions, and file-scope static remain TU-local\n"
      << "build.native_interop: Linux x86_64/aarch64 executable builds accept "
         "explicit C/C++ sources, objects, archives, shared libraries, and "
         "native linker arguments; C++ crosses the boundary through extern \"C\"\n"
      << "build.cargo: Linux x86_64/aarch64 executable builds accept one Cargo "
         "staticlib selected from Cargo JSON output and forward supported "
         "native library requirements\n"
      << "abi.integer.parameters: 1/2/4/8 byte integer scalars\n"
      << "abi.float.lengths: 2/4/8/16 byte float operations when supported by "
         "LLVM target lowering\n"
      << "float.fallback: f16 math evaluates as f32 and rounds back to "
         "binary16; f128 uses the target-specific runtime backend\n"
      << "abi.core.return.single: scalar integer or void; fN signatures preserve "
         "their floating ABI type\n"
      << "abi.core.return.multiple: LLVM aggregate retaining integer fields and "
         "fN floating fields\n"
      << "abi.c_compat: --c-compat applies C ABI metadata after core sema\n"
      << "abi.c: explicit extern \"C\" imports and exports use unmangled C "
         "symbols; scalar bool/integer/f32/f64 and addr/cstr/handle values "
         "are supported; aggregates, f16, f128, multiple returns, varargs, "
         "and exception crossing are rejected\n"
      << "abi.c_compat.scalar: integer, float/double, and pointer parameters "
         "and returns\n"
      << "abi.c_compat.aggregate_by_value: only x86_64 SysV ELF targets; other "
         "targets reject C struct by-value parameters and returns; supported "
         "direct aggregates up to 16 bytes use at most two INTEGER/SSE "
         "eightbytes\n"
      << "abi.c_compat.aggregate_indirect: on supported x86_64 SysV ELF targets, "
         "aggregates larger than 16 bytes use sret returns and byval parameters\n"
      << "abi.c_compat.alignment: LLVM DataLayout validates C struct size, "
         "alignment, and field offsets for the selected LLVM target\n"
      << "abi.c_compat.aggregate_limits: only supported integer/pointer and "
         "float/double leaves participate; other SysV classes, packed or "
         "bit-field layouts, unions, vectors, and long double aggregates are "
         "not covered\n"
      << "abi.c_compat.arrays: fixed C arrays retain element ABI shape for "
         "globals and external declarations\n"
      << "stdlib.formatting: native printf, print, fprintf, scanf, and fscanf "
         "use runtime descriptors; C compatibility does not accept variadic "
         "declarations\n"
      << "abi.pointer: unsigned integer with pointer.length bytes\n"
      << "abi.string: zero-terminated UTF-8 byte sequence\n"
      << "abi.file_handle: host FILE* represented as pointer.length bytes\n"
      << "stdlib.headers: stdlib.hsh, string.hsh, stdio.hsh, math.hsh, "
         "ctype.hsh, time.hsh, and assert.hsh; each standard function "
         "requires its owning system header\n"
      << "stdlib.provider.selection: optimized is the default; reference uses "
         "a manifest-declared reference implementation when available and "
         "otherwise keeps the default provider\n"
      << "stdlib.provider.dispatch: provider selection is compile-time and "
         "does not change the public View contract or safety mode\n"
      << "stdlib.print.template: standard fixed templates lower to "
         "printf-style print; user templates require a matching op format "
         "candidate in sema\n"
      << "stdlib.raw_output: print(view as none), put, and fput write the "
         "complete View and return the written byte count\n"
      << "stdlib.assignment.cstr: default '=' from string literal lowers to "
         "NUL-terminated string store\n"
      << "syntax.terminators: repeated NEWLINE and ';' are accepted as empty "
         "terminators in top-level, block, struct, template, and impl lists\n"
      << "compat.c: enabled with --c-compat; separate C AST lowers to core AST\n"
      << "impl.mut_self: diagnostic; mut self and mut impl parameters are "
         "reserved in Beta.21\n"
      << "stdlib.unsupported: none in current core bridge group\n";
}

int runHsc(const std::vector<std::string>& arguments) {
  if (arguments.size() == 1U) {
    std::cerr << "hsc: missing input file\n";
    return EXIT_FAILURE;
  }

  hitsimple::support::CompilationMetrics metrics;
  auto parsed = parseDriverOptions(arguments);
  if (!parsed.ok()) {
    std::cerr << "hsc: " << parsed.error << '\n';
    metrics.fail("cli");
    if (parsed.shouldPrintTiming) {
      metrics.printSummary(std::cerr);
    }
    if (parsed.timingJsonPath) {
      std::string error;
      if (!hitsimple::support::writeTimingJsonAtomically(
              hitsimple::support::pathFromUtf8(*parsed.timingJsonPath),
              metrics, error)) {
        std::cerr << "hsc: " << error << '\n';
      }
    }
    return EXIT_FAILURE;
  }

  auto options = std::move(*parsed.options);
  options.codegenOptions.targetTriple = targetTriple();
  auto& shouldDumpTokens = options.shouldDumpTokens;
  auto& shouldDumpAst = options.shouldDumpAst;
  auto& shouldDumpHir = options.shouldDumpHir;
  auto& shouldEmitLlvm = options.shouldEmitLlvm;
  auto& shouldPreprocessOnly = options.shouldPreprocessOnly;
  auto& shouldPrintTargetInfo = options.shouldPrintTargetInfo;
  auto& cCompatibilityMode = options.cCompatibilityMode;
  auto& shouldPrintTiming = options.shouldPrintTiming;
  auto& requestedDiagnosticFormat = options.diagnosticOutputFormat;
  auto& crateType = options.crateType;
  auto& providerSelection = options.providerSelection;
  auto& codegenOptions = options.codegenOptions;
  auto& backendOptions = options.backendOptions;
  auto& inputPaths = options.inputPaths;
  auto& outputPath = options.outputPath;
  auto& timingJsonPath = options.timingJsonPath;
  auto& clangOverride = options.clangOverride;
  auto& clangxxOverride = options.clangxxOverride;
  auto& externalInputs = options.externalInputs;
  auto& cargoManifest = options.cargoManifest;
  auto& cargoPackage = options.cargoPackage;
  auto& cargoProfile = options.cargoProfile;
  auto& cargoFeatures = options.cargoFeatures;
  auto& cargoNoDefaultFeatures = options.cargoNoDefaultFeatures;

  const auto finish = [&](int exitCode) {
    if (exitCode == EXIT_SUCCESS) {
      metrics.succeed();
    } else if (!metrics.failedStage()) {
      metrics.fail("cli");
    }
    if (shouldPrintTiming) {
      metrics.printSummary(std::cerr);
    }
    if (timingJsonPath) {
      std::string error;
      if (!hitsimple::support::writeTimingJsonAtomically(
              hitsimple::support::pathFromUtf8(*timingJsonPath), metrics,
              error)) {
        std::cerr << "hsc: " << error << '\n';
        return EXIT_FAILURE;
      }
    }
    return exitCode;
  };

  if (options.showHelp) {
    printHelp(std::cout);
    return finish(EXIT_SUCCESS);
  }
  if (options.showVersion) {
    printVersion(std::cout);
    return finish(EXIT_SUCCESS);
  }

  const DiagnosticOutputFormatScope diagnosticFormatScope(
      requestedDiagnosticFormat);
  std::vector<std::string_view> actions;
  if (shouldDumpTokens) {
    actions.push_back("--dump-tokens");
  }
  if (shouldDumpAst) {
    actions.push_back("--dump-ast");
  }
  if (shouldDumpHir) {
    actions.push_back("--dump-hir");
  }
  if (shouldEmitLlvm) {
    actions.push_back("--emit-llvm");
  }
  if (shouldPreprocessOnly) {
    actions.push_back("--preprocess-only");
  }
  if (shouldPrintTargetInfo) {
    actions.push_back("--target-info");
  }

  if (backendOptions.pgoMode == PgoMode::Use) {
    std::error_code profileError;
    if (!std::filesystem::exists(backendOptions.profilePath, profileError)) {
      std::cerr << "hsc: PGO profile data does not exist '"
                << hitsimple::support::pathToUtf8(backendOptions.profilePath)
                << "'\n";
      return finish(EXIT_FAILURE);
    }
    if (!std::filesystem::is_regular_file(backendOptions.profilePath,
                                          profileError)) {
      std::cerr << "hsc: PGO profile data is not a regular file '"
                << hitsimple::support::pathToUtf8(backendOptions.profilePath)
                << "'\n";
      return finish(EXIT_FAILURE);
    }
  }

  if (timingJsonPath) {
    std::string error;
    const auto timingPath = hitsimple::support::pathFromUtf8(*timingJsonPath);
    if (!hitsimple::support::timingOutputPathIsValid(timingPath, error)) {
      std::cerr << "hsc: " << error << '\n';
      timingJsonPath.reset();
      return finish(EXIT_FAILURE);
    }
    if (outputPath && outputPathsConflict(*outputPath, *timingJsonPath)) {
      std::cerr << "hsc: --timing-json path must not match -o output path\n";
      timingJsonPath.reset();
      return finish(EXIT_FAILURE);
    }
    if (backendOptions.optimizationRemarksPath &&
        outputPathsConflict(
            hitsimple::support::pathToUtf8(*backendOptions.optimizationRemarksPath),
            *timingJsonPath)) {
      std::cerr << "hsc: --optimization-remarks path must not match "
                   "--timing-json path\n";
      timingJsonPath.reset();
      return finish(EXIT_FAILURE);
    }
    if (!outputPath && actions.empty()) {
#ifdef _WIN32
      constexpr std::string_view defaultOutputPath = "a.exe";
#else
      constexpr std::string_view defaultOutputPath = "a.out";
#endif
      const auto selectedOutputPath = crateType == CrateType::Object
          ? std::string_view{"a.o"}
          : (crateType == CrateType::StaticLib ? std::string_view{"libhitsimple.a"}
                                                : defaultOutputPath);
      if (outputPathsConflict(std::string(selectedOutputPath), *timingJsonPath)) {
        std::cerr << "hsc: --timing-json path must not match default output path\n";
        timingJsonPath.reset();
        return finish(EXIT_FAILURE);
      }
    }
  }

  const auto rejectOutputPath = [&](std::string_view action) {
    if (outputPath) {
      std::cerr << "hsc: -o is not supported with " << action << '\n';
      return true;
    }
    return false;
  };

  if (shouldDumpTokens) {
    if (rejectOutputPath("--dump-tokens")) {
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-tokens requires an input file\n";
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-tokens supports exactly one input file\n";
      return finish(EXIT_FAILURE);
    }
    return finish(dumpTokens(inputPaths.front()));
  }

  if (shouldPrintTargetInfo) {
    if (rejectOutputPath("--target-info")) {
      return finish(EXIT_FAILURE);
    }
    if (!inputPaths.empty()) {
      std::cerr << "hsc: --target-info does not take an input file\n";
      return finish(EXIT_FAILURE);
    }
    printTargetInfo(std::cout, hitsimple::support::resolveClang(clangOverride));
    return finish(EXIT_SUCCESS);
  }

  if (shouldDumpAst) {
    if (rejectOutputPath("--dump-ast")) {
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-ast requires an input file\n";
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-ast supports exactly one input file\n";
      return finish(EXIT_FAILURE);
    }
    return finish(dumpAst(inputPaths.front(), cCompatibilityMode));
  }

  if (shouldDumpHir) {
    if (rejectOutputPath("--dump-hir")) {
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-hir requires an input file\n";
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-hir supports exactly one input file\n";
      return finish(EXIT_FAILURE);
    }
    return finish(dumpHir(inputPaths.front(), cCompatibilityMode,
                          providerSelection));
  }

  if (shouldEmitLlvm) {
    if (inputPaths.empty()) {
      std::cerr << "hsc: --emit-llvm requires an input file\n";
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --emit-llvm supports exactly one input file\n";
      return finish(EXIT_FAILURE);
    }
    return finish(emitLlvm(inputPaths.front(), outputPath, codegenOptions,
                           cCompatibilityMode, providerSelection, metrics));
  }

  if (shouldPreprocessOnly) {
    if (inputPaths.empty()) {
      std::cerr << "hsc: --preprocess-only requires an input file\n";
      return finish(EXIT_FAILURE);
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --preprocess-only supports exactly one input file\n";
      return finish(EXIT_FAILURE);
    }
    return finish(preprocessOnly(inputPaths.front(), outputPath));
  }

  if (inputPaths.empty()) {
    std::cerr << "hsc: missing input file\n";
    return finish(EXIT_FAILURE);
  }

  if (backendOptions.optimizationRemarksPath) {
#ifdef _WIN32
    constexpr std::string_view defaultExecutablePath = "a.exe";
#else
    constexpr std::string_view defaultExecutablePath = "a.out";
#endif
    const auto selectedOutputPath = outputPath.value_or(
        crateType == CrateType::Object
            ? defaultObjectOutputPath(inputPaths.front())
            : (crateType == CrateType::StaticLib ? "libhitsimple.a"
                                                 : std::string(defaultExecutablePath)));
    if (outputPathsConflict(
            selectedOutputPath,
            hitsimple::support::pathToUtf8(*backendOptions.optimizationRemarksPath))) {
      std::cerr << "hsc: --optimization-remarks path must not match native "
                   "output path\n";
      return finish(EXIT_FAILURE);
    }
    if (!prepareOptimizationRemarksOutput(*backendOptions.optimizationRemarksPath)) {
      return finish(EXIT_FAILURE);
    }
  }

  if (crateType == CrateType::Object) {
    return finish(compileObject(inputPaths, outputPath, codegenOptions,
                                cCompatibilityMode, providerSelection,
                                hitsimple::support::resolveClang(clangOverride),
                                backendOptions, metrics));
  }
  if (crateType == CrateType::StaticLib) {
    return finish(compileStaticLibrary(
        inputPaths, outputPath, codegenOptions, cCompatibilityMode,
        providerSelection,
        hitsimple::support::resolveLlvmAr(),
        hitsimple::support::resolveClang(clangOverride), backendOptions,
        metrics));
  }

  if (cargoManifest) {
    hitsimple::support::CargoBuildOptions cargoOptions{
        *cargoManifest, cargoPackage, cargoProfile, cargoFeatures,
        cargoNoDefaultFeatures};
    std::string cargoError;
    const auto cargoLibrary = hitsimple::support::buildCargoStaticLibrary(
        hitsimple::support::resolveCargo(), cargoOptions, cargoError);
    if (!cargoLibrary) {
      std::cerr << "hsc: " << cargoError << '\n';
      return finish(EXIT_FAILURE);
    }
    externalInputs.linkInputs.push_back(
        hitsimple::support::pathToUtf8(cargoLibrary->archivePath));
    externalInputs.libraryDirectories.insert(
        externalInputs.libraryDirectories.end(),
        cargoLibrary->libraryDirectories.begin(),
        cargoLibrary->libraryDirectories.end());
    externalInputs.libraries.insert(externalInputs.libraries.end(),
                                    cargoLibrary->libraries.begin(),
                                    cargoLibrary->libraries.end());
    if (!externalInputs.linkerLanguage && externalInputs.cxxSources.empty()) {
      externalInputs.linkerLanguage = LinkerLanguage::C;
    }
  }

  if (externalInputs.hasMixedBuildOptions()) {
    return finish(compileMixedExecutable(
        inputPaths, outputPath, codegenOptions, cCompatibilityMode,
        providerSelection,
        clangOverride, clangxxOverride, externalInputs, backendOptions,
        metrics));
  }

  return finish(compileExecutable(
      inputPaths, outputPath, codegenOptions, cCompatibilityMode,
      providerSelection,
      hitsimple::support::resolveClang(clangOverride), backendOptions,
      metrics));
}

} // namespace hitsimple::driver
