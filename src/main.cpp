#include "hitsimple/lexer/Lexer.h"
#include "hitsimple/ast/AST.h"
#include "hitsimple/compat/CCompat.h"
#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/codegen/OptimizationPipeline.h"
#include "hitsimple/codegen/TargetCapabilities.h"
#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/sema/Sema.h"
#include "hitsimple/stdlib/StandardLibraryModules.h"
#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Cargo.h"
#include "hitsimple/support/ResourcePaths.h"
#include "hitsimple/support/Toolchain.h"

#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

enum class CrateType {
  Bin,
  Object,
  StaticLib,
};

enum class EntryMode {
  HitSimple,
  Native,
};

enum class DiagnosticOutputFormat {
  Human,
  Json,
};

enum class LinkerLanguage {
  C,
  Cxx,
};

struct ExternalBuildInputs final {
  std::vector<std::string> cSources;
  std::vector<std::string> cxxSources;
  std::vector<std::string> linkInputs;
  std::vector<std::string> libraryDirectories;
  std::vector<std::string> libraries;
  std::vector<std::string> linkArguments;
  std::optional<LinkerLanguage> linkerLanguage;
  EntryMode entryMode = EntryMode::HitSimple;

  bool hasExternalInputs() const {
    return !cSources.empty() || !cxxSources.empty() || !linkInputs.empty() ||
           !libraryDirectories.empty() || !libraries.empty() ||
           !linkArguments.empty();
  }

  bool hasMixedBuildOptions() const {
    return hasExternalInputs() || linkerLanguage.has_value() ||
           entryMode != EntryMode::HitSimple;
  }
};

using OptimizationLevel = hitsimple::codegen::OptimizationLevel;
using PgoMode = hitsimple::codegen::PgoMode;

struct NativeBackendOptions final {
  OptimizationLevel optimization = OptimizationLevel::O2;
  PgoMode pgoMode = PgoMode::None;
  std::filesystem::path profilePath;
  std::optional<std::filesystem::path> optimizationRemarksPath;
};

std::string_view clangOptimizationFlag(OptimizationLevel level) {
  switch (level) {
  case OptimizationLevel::O0:
    return "-O0";
  case OptimizationLevel::O1:
    return "-O1";
  case OptimizationLevel::O2:
    return "-O2";
  case OptimizationLevel::O3:
    return "-O3";
  case OptimizationLevel::Os:
    return "-Os";
  }
  return "-O2";
}

void appendClangCodegenArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
  arguments.emplace_back(clangOptimizationFlag(backendOptions.optimization));
  switch (backendOptions.pgoMode) {
  case PgoMode::None:
    return;
  case PgoMode::Instrument:
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
    return;
  case PgoMode::Use:
    arguments.push_back(
        "-fprofile-instr-use=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
    return;
  }
}

void appendHitSimpleIrCodegenArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
#if defined(__APPLE__)
  // Clang normalizes LLVM's native Darwin host triple to its macOS deployment
  // triple for the same ABI. Suppress only that expected driver diagnostic.
  arguments.emplace_back("-Wno-override-module");
#endif
  // The embedded New Pass Manager already applied the requested optimization
  // pipeline. Keep Clang in backend-only mode so it cannot silently select a
  // second, potentially different IR optimization pipeline.
  arguments.emplace_back("-O0");
  if (backendOptions.pgoMode == PgoMode::Instrument) {
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
  }
}

void appendClangLinkArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
  arguments.emplace_back(clangOptimizationFlag(backendOptions.optimization));
  if (backendOptions.pgoMode == PgoMode::Instrument) {
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
  }
}

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

std::string targetTriple() {
#ifdef _WIN32
  return "x86_64-w64-windows-gnu";
#else
  return llvm::sys::getDefaultTargetTriple();
#endif
}

bool usesWindowsCodeView(
    const hitsimple::codegen::CodegenOptions& codegenOptions) {
  const auto triple = codegenOptions.targetTriple.empty()
      ? targetTriple()
      : codegenOptions.targetTriple;
  return llvm::Triple(triple).isOSWindows();
}

std::filesystem::path pdbPathForExecutable(
    const std::string& executablePath) {
  auto pdbPath = hitsimple::support::pathFromUtf8(executablePath);
  pdbPath.replace_extension(".pdb");
  return pdbPath;
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

DiagnosticOutputFormat diagnosticOutputFormat = DiagnosticOutputFormat::Human;

class DiagnosticOutputFormatScope final {
public:
  explicit DiagnosticOutputFormatScope(DiagnosticOutputFormat format)
      : previous_(diagnosticOutputFormat) {
    diagnosticOutputFormat = format;
  }

  ~DiagnosticOutputFormatScope() { diagnosticOutputFormat = previous_; }

  DiagnosticOutputFormatScope(const DiagnosticOutputFormatScope&) = delete;
  DiagnosticOutputFormatScope& operator=(const DiagnosticOutputFormatScope&) =
      delete;

private:
  DiagnosticOutputFormat previous_;
};

void printHumanDiagnostic(const hitsimple::diagnostic::Diagnostic& diagnostic) {
  std::cerr << "hsc: " << diagnostic.format() << '\n';
  const auto excerpt = hitsimple::diagnostic::renderSourceExcerpt(diagnostic);
  if (!excerpt.empty()) {
    std::cerr << excerpt << '\n';
  }
}

void printDiagnostic(const hitsimple::diagnostic::Diagnostic& diagnostic) {
  if (diagnosticOutputFormat == DiagnosticOutputFormat::Json) {
    std::cerr << diagnostic.formatJson() << '\n';
    return;
  }

  printHumanDiagnostic(diagnostic);
  for (const auto& label : diagnostic.labels) {
    auto note = hitsimple::diagnostic::Diagnostic::error(
        diagnostic.stage, label.message);
    note.severity = hitsimple::diagnostic::Severity::Note;
    note.range = label.range;
    printHumanDiagnostic(note);
  }
}

bool printDiagnostics(const std::vector<hitsimple::diagnostic::Diagnostic>& diagnostics) {
  bool hasError = false;
  for (const auto& diagnostic : diagnostics) {
    printDiagnostic(diagnostic);
    hasError = hasError ||
               diagnostic.severity == hitsimple::diagnostic::Severity::Error;
  }
  return hasError;
}

hitsimple::diagnostic::SourceRange
fileStartRange(std::string_view inputPath) {
  const hitsimple::diagnostic::SourceLocation location{std::string(inputPath),
                                                        1, 1};
  return {location, location};
}

hitsimple::diagnostic::Diagnostic
fileLevelDiagnostic(hitsimple::diagnostic::Stage stage, std::string message,
                    std::string_view inputPath) {
  auto diagnostic = hitsimple::diagnostic::Diagnostic::error(
      stage, std::move(message));
  if (!inputPath.empty()) {
    diagnostic.range = fileStartRange(inputPath);
  }
  return diagnostic;
}

std::string escapeLexeme(std::string_view lexeme) {
  std::string escaped;
  for (const char ch : lexeme) {
    switch (ch) {
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '`':
      escaped += "\\`";
      break;
    default:
      escaped += ch;
      break;
    }
  }
  return escaped;
}

bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream output(hitsimple::support::pathFromUtf8(path),
                       std::ios::binary);
  if (!output) {
    return false;
  }

  output << content;
  return static_cast<bool>(output);
}

bool validateOutputParent(const std::string& path) {
  const auto parent = hitsimple::support::pathFromUtf8(path).parent_path();
  if (parent.empty()) {
    return true;
  }
  if (!std::filesystem::exists(parent)) {
    std::cerr << "hsc: output directory does not exist '"
              << hitsimple::support::pathToUtf8(parent) << "'\n";
    return false;
  }
  if (!std::filesystem::is_directory(parent)) {
    std::cerr << "hsc: output parent is not a directory '"
              << hitsimple::support::pathToUtf8(parent) << "'\n";
    return false;
  }
  return true;
}

bool outputPathsConflict(const std::string& left, const std::string& right) {
  const auto normalized = [](const std::string& path) {
    return std::filesystem::absolute(hitsimple::support::pathFromUtf8(path))
        .lexically_normal();
  };
  return normalized(left) == normalized(right);
}

std::optional<std::string> readFile(const std::string& path) {
  std::ifstream input(hitsimple::support::pathFromUtf8(path),
                      std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool prepareOptimizationRemarksOutput(const std::filesystem::path& path) {
  const auto pathText = hitsimple::support::pathToUtf8(path);
  if (!validateOutputParent(pathText)) {
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::cerr << "hsc: cannot write optimization remarks '" << pathText
              << "'\n";
    return false;
  }
  return true;
}

bool appendOptimizationRemarks(const std::filesystem::path& path,
                               const std::vector<std::string>& remarks) {
  if (remarks.empty()) {
    return true;
  }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) {
    std::cerr << "hsc: cannot append optimization remarks '"
              << hitsimple::support::pathToUtf8(path) << "'\n";
    return false;
  }
  for (const auto& remark : remarks) {
    output << remark << '\n';
  }
  return static_cast<bool>(output);
}

std::optional<unsigned> backendClangMajorVersion(
    const hitsimple::support::ClangSelection& clang, std::string& error) {
  if (!clang.path) {
    error = clang.error;
    return std::nullopt;
  }
  std::error_code filesystemError;
  const auto outputPath = std::filesystem::temp_directory_path(filesystemError) /
      ("hitsimple-clang-version-" +
       std::to_string(hitsimple::support::currentProcessId()) + ".txt");
  if (filesystemError) {
    error = "cannot determine a temporary path for Clang version detection: " +
            filesystemError.message();
    return std::nullopt;
  }
  const auto process =
      hitsimple::support::runProcess(*clang.path, {"--version"}, outputPath);
  const auto output = readFile(hitsimple::support::pathToUtf8(outputPath));
  std::filesystem::remove(outputPath, filesystemError);
  if (!process.launched) {
    error = "cannot start backend Clang '" +
            hitsimple::support::pathToUtf8(*clang.path) + "': " +
            process.error;
    return std::nullopt;
  }
  if (process.exitCode != 0 || !output) {
    error = "cannot read backend Clang version from '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  const auto marker = output->find("clang version ");
  if (marker == std::string::npos) {
    error = "backend compiler does not report a Clang version: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  auto cursor = marker + std::string_view("clang version ").size();
  const auto firstDigit = cursor;
  while (cursor < output->size() &&
         std::isdigit(static_cast<unsigned char>((*output)[cursor]))) {
    ++cursor;
  }
  if (cursor == firstDigit) {
    error = "backend Clang has an invalid version string: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  try {
    return static_cast<unsigned>(std::stoul(output->substr(
        firstDigit, cursor - firstDigit)));
  } catch (const std::exception&) {
    error = "backend Clang has an unsupported version string: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
}

bool validateBackendClangCompatibility(
    const hitsimple::support::ClangSelection& clang) {
  std::string error;
  const auto major = backendClangMajorVersion(clang, error);
  if (!major) {
    std::cerr << "hsc: " << error << '\n';
    return false;
  }
  if (*major != LLVM_VERSION_MAJOR) {
    std::cerr << "hsc: embedded LLVM major version " << LLVM_VERSION_MAJOR
              << " is incompatible with backend Clang major version " << *major
              << "; select a matching --clang executable\n";
    return false;
  }
  return true;
}

struct ParsedTranslationUnit {
  std::unique_ptr<hitsimple::ast::TranslationUnit> unit;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<hitsimple::stdlib::StandardHeader> standardHeaders;
};

std::optional<ParsedTranslationUnit>
parseInput(const std::string& inputPath, bool cCompatibilityMode) {
  auto preprocessed = hitsimple::preprocessor::preprocessFile(inputPath);
  if (printDiagnostics(preprocessed.diagnostics)) {
    return std::nullopt;
  }

  if (cCompatibilityMode) {
    auto parsed = hitsimple::compat::parseCCompatSource(preprocessed.source,
                                                         inputPath);
    if (printDiagnostics(parsed.diagnostics) || !parsed.unit) {
      if (!parsed.unit && parsed.diagnostics.empty()) {
        std::cerr << "hsc: C compatibility parser did not produce an AST\n";
      }
      return std::nullopt;
    }

    auto loweringOptions = hitsimple::compat::LoweringOptions{};
    loweringOptions.allowHostFloatExternAbi = true;
    auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit,
                                                          loweringOptions);
    if (printDiagnostics(lowered.diagnostics) || !lowered.unit) {
      if (!lowered.unit && lowered.diagnostics.empty()) {
        std::cerr << "hsc: C compatibility lowering did not produce a core AST\n";
      }
      return std::nullopt;
    }
    return ParsedTranslationUnit{std::move(lowered.unit),
                                 std::move(lowered.linkage), {}};
  }

  auto parseResult = hitsimple::parser::parseSource(
      preprocessed.source, inputPath, std::move(preprocessed.lineOrigins));
  if (printDiagnostics(parseResult.diagnostics) || !parseResult.unit) {
    return std::nullopt;
  }

  return ParsedTranslationUnit{std::move(parseResult.unit), {},
                               std::move(preprocessed.standardHeaders)};
}

std::optional<ParsedTranslationUnit> parseInput(
    const std::string& inputPath, bool cCompatibilityMode,
    hitsimple::support::CompilationMetrics& metrics,
    hitsimple::support::TranslationUnitMetrics& unitMetrics) {
  const auto preprocessStarted = metrics.now();
  auto preprocessed = hitsimple::preprocessor::preprocessFile(inputPath);
  if (printDiagnostics(preprocessed.diagnostics)) {
    metrics.fail(unitMetrics.preprocess, preprocessStarted);
    metrics.fail("preprocess");
    return std::nullopt;
  }
  metrics.complete(unitMetrics.preprocess, preprocessStarted);

  const auto parseStarted = metrics.now();
  if (cCompatibilityMode) {
    auto parsed = hitsimple::compat::parseCCompatSource(preprocessed.source,
                                                         inputPath);
    if (printDiagnostics(parsed.diagnostics) || !parsed.unit) {
      if (!parsed.unit && parsed.diagnostics.empty()) {
        std::cerr << "hsc: C compatibility parser did not produce an AST\n";
      }
      metrics.fail(unitMetrics.parse, parseStarted);
      metrics.fail("parse");
      return std::nullopt;
    }
    metrics.complete(unitMetrics.parse, parseStarted);

    const auto loweringStarted = metrics.now();
    auto loweringOptions = hitsimple::compat::LoweringOptions{};
    loweringOptions.allowHostFloatExternAbi = true;
    auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit,
                                                          loweringOptions);
    if (printDiagnostics(lowered.diagnostics) || !lowered.unit) {
      if (!lowered.unit && lowered.diagnostics.empty()) {
        std::cerr << "hsc: C compatibility lowering did not produce a core AST\n";
      }
      metrics.fail(unitMetrics.cCompatLowering, loweringStarted);
      metrics.fail("c_compat_lowering");
      return std::nullopt;
    }
    metrics.complete(unitMetrics.cCompatLowering, loweringStarted);
    return ParsedTranslationUnit{std::move(lowered.unit),
                                 std::move(lowered.linkage), {}};
  }

  auto parseResult = hitsimple::parser::parseSource(
      preprocessed.source, inputPath, std::move(preprocessed.lineOrigins));
  if (printDiagnostics(parseResult.diagnostics) || !parseResult.unit) {
    metrics.fail(unitMetrics.parse, parseStarted);
    metrics.fail("parse");
    return std::nullopt;
  }
  metrics.complete(unitMetrics.parse, parseStarted);
  metrics.markSkipped(unitMetrics.cCompatLowering);
  return ParsedTranslationUnit{std::move(parseResult.unit), {},
                               std::move(preprocessed.standardHeaders)};
}

std::size_t mainDefinitionCount(const hitsimple::ast::TranslationUnit& unit) {
  std::size_t count = 0;
  for (const auto* function : unit.functions) {
    if (function->name == "main") {
      ++count;
    }
  }
  return count;
}

struct CompiledTranslationUnit {
  std::string inputPath;
  std::string llvmIr;
  std::size_t mainDefinitionCount = 0;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<std::string> sourceModules;
};

bool sameCAbiType(const hitsimple::compat::CAbiType& left,
                  const hitsimple::compat::CAbiType& right) {
  if (left.kind != right.kind || left.byteLength != right.byteLength ||
      left.isSigned != right.isSigned ||
      left.aggregateName != right.aggregateName ||
      left.alignment != right.alignment ||
      left.elementCount != right.elementCount ||
      left.aggregateFieldOffsets != right.aggregateFieldOffsets ||
      left.aggregateFields.size() != right.aggregateFields.size()) {
    return false;
  }
  return std::equal(left.aggregateFields.begin(), left.aggregateFields.end(),
                    right.aggregateFields.begin(),
                    [](const hitsimple::compat::CAbiType& leftField,
                       const hitsimple::compat::CAbiType& rightField) {
                      return sameCAbiType(leftField, rightField);
                    });
}

bool sameCExternalAbi(const hitsimple::compat::LinkageMetadata& left,
                      const hitsimple::compat::LinkageMetadata& right) {
  if (left.isFunction != right.isFunction) {
    return false;
  }
  if (!left.isFunction) {
    return left.objectType.has_value() == right.objectType.has_value() &&
           (!left.objectType || sameCAbiType(*left.objectType, *right.objectType));
  }
  if (left.parameterTypes.size() != right.parameterTypes.size() ||
      left.returnType.has_value() != right.returnType.has_value()) {
    return false;
  }
  if (left.returnType && !sameCAbiType(*left.returnType, *right.returnType)) {
    return false;
  }
  return std::equal(left.parameterTypes.begin(), left.parameterTypes.end(),
                    right.parameterTypes.begin(),
                    [](const hitsimple::compat::CAbiType& leftParameter,
                       const hitsimple::compat::CAbiType& rightParameter) {
                      return sameCAbiType(leftParameter, rightParameter);
                    });
}

bool validateCCompatibilityExternalAbi(
    const std::vector<CompiledTranslationUnit>& units) {
  struct Declaration final {
    const CompiledTranslationUnit* unit = nullptr;
    const hitsimple::compat::LinkageMetadata* metadata = nullptr;
  };
  std::unordered_map<std::string, Declaration> declarations;
  for (const auto& unit : units) {
    for (const auto& item : unit.compatibilityLinkage) {
      if (item.linkage != hitsimple::compat::Linkage::External) {
        continue;
      }
      const auto [found, inserted] =
          declarations.emplace(item.coreName, Declaration{&unit, &item});
      if (inserted || sameCExternalAbi(*found->second.metadata, item)) {
        continue;
      }
      const auto& name = item.sourceName.empty() ? item.coreName : item.sourceName;
      const auto sourcePath = !item.range.begin.file.empty()
                                  ? item.range.begin.file
                                  : unit.inputPath;
      auto diagnostic = fileLevelDiagnostic(
          hitsimple::diagnostic::Stage::Sema,
          "incompatible C external declaration '" + name +
              "' across translation units",
          sourcePath);
      const auto& previous = *found->second.metadata;
      const auto previousPath = !previous.range.begin.file.empty()
                                    ? previous.range.begin.file
                                    : found->second.unit->inputPath;
      if (!previousPath.empty()) {
        diagnostic.labels.push_back(
            {fileStartRange(previousPath), "previous external declaration is here"});
      }
      printDiagnostic(diagnostic);
      return false;
    }
  }
  return true;
}

std::optional<hitsimple::hir::AbiType> lowerCAbiType(
    const hitsimple::compat::CAbiType& type, std::string_view symbolName,
    std::string_view role) {
  using CAbiValueKind = hitsimple::compat::CAbiValueKind;
  using HirAbiValueKind = hitsimple::hir::AbiValueKind;

  switch (type.kind) {
  case CAbiValueKind::Integer:
  {
    hitsimple::hir::AbiType lowered{HirAbiValueKind::Integer, type.byteLength,
                                    type.isSigned};
    lowered.alignment = type.alignment;
    lowered.elementCount = type.elementCount;
    return lowered;
  }
  case CAbiValueKind::Floating:
  {
    hitsimple::hir::AbiType lowered{HirAbiValueKind::Floating, type.byteLength,
                                    type.isSigned};
    lowered.alignment = type.alignment;
    lowered.elementCount = type.elementCount;
    return lowered;
  }
  case CAbiValueKind::Pointer:
  {
    hitsimple::hir::AbiType lowered{HirAbiValueKind::Pointer, type.byteLength,
                                    type.isSigned};
    lowered.alignment = type.alignment;
    lowered.elementCount = type.elementCount;
    return lowered;
  }
  case CAbiValueKind::Void:
    std::cerr << "hsc: C compatibility ABI " << role << " for '" << symbolName
              << "' cannot have void type\n";
    return std::nullopt;
  case CAbiValueKind::Aggregate:
  {
    if (type.byteLength == 0 || type.alignment == 0 ||
        type.aggregateFields.size() != type.aggregateFieldOffsets.size()) {
      std::cerr << "hsc: C aggregate ABI " << role << " for '" << symbolName
                << "' has an invalid layout\n";
      return std::nullopt;
    }
    hitsimple::hir::AbiType lowered{HirAbiValueKind::Aggregate, type.byteLength,
                                    false};
    lowered.aggregateName = type.aggregateName;
    lowered.alignment = type.alignment;
    lowered.elementCount = type.elementCount;
    lowered.aggregateFieldOffsets = type.aggregateFieldOffsets;
    lowered.aggregateFields.reserve(type.aggregateFields.size());
    for (const auto& field : type.aggregateFields) {
      auto loweredField = lowerCAbiType(field, symbolName, "aggregate field");
      if (!loweredField) {
        return std::nullopt;
      }
      lowered.aggregateFields.push_back(std::move(*loweredField));
    }
    return lowered;
  }
  }
  return std::nullopt;
}

bool applyCCompatibilityMetadata(
    hitsimple::hir::TranslationUnit& unit,
    const std::vector<hitsimple::compat::LinkageMetadata>& metadata) {
  std::vector<hitsimple::hir::LinkageOverride> linkageOverrides;
  std::vector<hitsimple::hir::AbiOverride> abiOverrides;
  bool hasError = false;

  for (const auto& item : metadata) {
    if (item.coreName.empty()) {
      std::cerr << "hsc: C compatibility linkage metadata has an empty core name\n";
      hasError = true;
      continue;
    }

    if (item.isDefinition &&
        item.linkage == hitsimple::compat::Linkage::Internal) {
      linkageOverrides.push_back(
          {item.coreName,
           item.isFunction ? hitsimple::hir::LinkageTarget::Function
                           : hitsimple::hir::LinkageTarget::Global,
           hitsimple::hir::Linkage::Internal});
    }

    if (item.isFunction) {
      if (!item.returnType) {
        std::cerr << "hsc: C compatibility function '" << item.coreName
                  << "' has no ABI return type metadata\n";
        hasError = true;
        continue;
      }

      hitsimple::hir::FunctionAbiSignature signature;
      signature.isCCompatibility = true;
      signature.parameterTypes.reserve(item.parameterTypes.size());
      for (const auto& parameter : item.parameterTypes) {
        auto lowered = lowerCAbiType(parameter, item.coreName, "parameter");
        if (!lowered) {
          hasError = true;
          continue;
        }
        signature.parameterTypes.push_back(*lowered);
      }
      if (item.returnType->kind != hitsimple::compat::CAbiValueKind::Void) {
        auto lowered = lowerCAbiType(*item.returnType, item.coreName, "return");
        if (!lowered) {
          hasError = true;
          continue;
        }
        signature.returnTypes.push_back(*lowered);
      }
      abiOverrides.push_back(
          {item.coreName, hitsimple::hir::LinkageTarget::Function, std::nullopt,
           std::move(signature)});
      continue;
    }

    if (!item.objectType) {
      std::cerr << "hsc: C compatibility global '" << item.coreName
                << "' has no ABI object type metadata\n";
      hasError = true;
      continue;
    }
    auto lowered = lowerCAbiType(*item.objectType, item.coreName, "object");
    if (!lowered) {
      hasError = true;
      continue;
    }

    const auto global = std::find_if(
        unit.globals.begin(), unit.globals.end(), [&item](const auto& candidate) {
          return candidate.bindingName == item.coreName;
        });
    if (global == unit.globals.end()) {
      std::cerr << "hsc: C compatibility ABI metadata references unknown global '"
                << item.coreName << "'\n";
      hasError = true;
      continue;
    }
    if (lowered->elementCount == 0 ||
        lowered->byteLength > std::numeric_limits<std::size_t>::max() /
                                  lowered->elementCount ||
        global->byteLength != lowered->byteLength * lowered->elementCount) {
      std::cerr << "hsc: C compatibility ABI object size does not match global '"
                << item.coreName << "'\n";
      hasError = true;
      continue;
    }
    abiOverrides.push_back(
        {item.coreName, hitsimple::hir::LinkageTarget::Global, *lowered,
         std::nullopt});
  }

  if (hasError) {
    return false;
  }
  if (printDiagnostics(
          hitsimple::hir::applyLinkageOverrides(unit, linkageOverrides))) {
    return false;
  }
  return !printDiagnostics(hitsimple::hir::applyAbiOverrides(unit, abiOverrides));
}

std::optional<CompiledTranslationUnit> compileTranslationUnit(
    const std::string& inputPath,
    hitsimple::codegen::CodegenOptions codegenOptions,
    bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    bool internalStandardModule,
    hitsimple::support::CompilationMetrics& metrics) {
  auto& unitMetrics = metrics.addTranslationUnit(inputPath);
  auto parsed = parseInput(inputPath, cCompatibilityMode, metrics, unitMetrics);
  if (!parsed) {
    return std::nullopt;
  }

  const auto mainCount = mainDefinitionCount(*parsed->unit);
  const auto semaStarted = metrics.now();
  auto analyzeResult = hitsimple::sema::analyze(
      *parsed->unit,
      hitsimple::sema::AnalyzeOptions{false, parsed->standardHeaders,
                                      cCompatibilityMode, internalStandardModule});
  if (!analyzeResult.unit) {
    printDiagnostics(analyzeResult.diagnostics);
    metrics.fail(unitMetrics.semaHir, semaStarted);
    metrics.fail("sema_hir");
    return std::nullopt;
  }
  if (cCompatibilityMode &&
      !applyCCompatibilityMetadata(*analyzeResult.unit,
                                   parsed->compatibilityLinkage)) {
    metrics.fail(unitMetrics.semaHir, semaStarted);
    metrics.fail("sema_hir");
    return std::nullopt;
  }
  const auto sourceModules = hitsimple::stdlib::selectStandardLibraryProviders(
      *analyzeResult.unit, providerSelection);
  unitMetrics.hirNodes =
      hitsimple::support::collectHirNodeMetrics(*analyzeResult.unit);
  metrics.complete(unitMetrics.semaHir, semaStarted);

  const auto emissionStarted = metrics.now();
  auto emitResult =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, inputPath,
                                     codegenOptions);
  if (!emitResult.diagnostics.empty()) {
    printDiagnostics(emitResult.diagnostics);
    metrics.fail(unitMetrics.llvmEmission, emissionStarted);
    metrics.fail("llvm_emission");
    return std::nullopt;
  }
  metrics.complete(unitMetrics.llvmEmission, emissionStarted);
  unitMetrics.llvmIrBytes = emitResult.llvmIr.size();

  return CompiledTranslationUnit{inputPath, std::move(emitResult.llvmIr), mainCount,
                                 std::move(parsed->compatibilityLinkage),
                                 sourceModules};
}

std::vector<std::string> collectRequiredSourceModules(
    const std::vector<CompiledTranslationUnit>& units) {
  std::vector<std::string> modules;
  std::unordered_set<std::string> seen;
  for (const auto& unit : units) {
    for (const auto& module : unit.sourceModules) {
      if (seen.insert(module).second) {
        modules.push_back(module);
      }
    }
  }
  return modules;
}

std::optional<std::vector<CompiledTranslationUnit>> compileSourceModules(
    const std::vector<std::string>& moduleIds,
    hitsimple::codegen::CodegenOptions codegenOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  std::vector<CompiledTranslationUnit> modules;
  modules.reserve(moduleIds.size());
  for (const auto& id : moduleIds) {
    const auto* module = hitsimple::stdlib::findSourceModule(id);
    if (module == nullptr) {
      std::cerr << "hsc: standard library source module '" << id
                << "' is not declared by the manifest\n";
      return std::nullopt;
    }
    const auto sourcePath = hitsimple::support::standardLibraryRoot() /
        std::string(module->file);
    if (!std::filesystem::is_regular_file(sourcePath)) {
      std::cerr << "hsc: standard library source module is unavailable '"
                << hitsimple::support::pathToUtf8(sourcePath) << "'\n";
      return std::nullopt;
    }
    auto compiled = compileTranslationUnit(
        hitsimple::support::pathToUtf8(sourcePath), codegenOptions, false,
        hitsimple::stdlib::BuiltinProviderSelection::Optimized, true, metrics);
    if (!compiled) {
      return std::nullopt;
    }
    modules.push_back(std::move(*compiled));
  }
  return modules;
}

std::optional<std::string> mergeLlvmModules(
    const std::vector<CompiledTranslationUnit>& units, std::string& error) {
  if (units.empty()) {
    error = "no LLVM modules to merge";
    return std::nullopt;
  }
  llvm::LLVMContext context;
  llvm::SMDiagnostic diagnostic;
  std::unique_ptr<llvm::Module> merged;
  for (std::size_t index = 0; index < units.size(); ++index) {
    auto buffer = llvm::MemoryBuffer::getMemBuffer(
        units[index].llvmIr, "hitsimple-module-" + std::to_string(index));
    auto module = llvm::parseAssembly(*buffer, diagnostic, context);
    if (!module) {
      std::string diagnosticText;
      llvm::raw_string_ostream stream(diagnosticText);
      diagnostic.print("hsc", stream);
      error = stream.str();
      return std::nullopt;
    }
    if (!merged) {
      merged = std::move(module);
      continue;
    }
    llvm::Linker linker(*merged);
    if (linker.linkInModule(std::move(module))) {
      error = "LLVM IR linker rejected an internal standard library module";
      return std::nullopt;
    }
  }
  std::string result;
  llvm::raw_string_ostream output(result);
  merged->print(output, nullptr);
  output.flush();
  return result;
}

int dumpTokens(const std::string& inputPath) {
  const auto source = readFile(inputPath);
  if (!source) {
    std::cerr << "hsc: cannot read input file '" << inputPath << "'\n";
    return EXIT_FAILURE;
  }

  const auto diagnostics =
      hitsimple::preprocessor::validateSource(*source, inputPath);
  if (printDiagnostics(diagnostics)) {
    return EXIT_FAILURE;
  }

  hitsimple::lexer::Lexer lexer(*source, inputPath);
  for (;;) {
    const auto token = lexer.next();
    std::cout << token.range.begin.line << ':' << token.range.begin.column
              << ' ' << hitsimple::lexer::tokenKindName(token.kind);
    if (!token.lexeme.empty()) {
      std::cout << " `" << escapeLexeme(token.lexeme) << '`';
    }
    std::cout << '\n';

    if (token.kind == hitsimple::lexer::TokenKind::End) {
      return EXIT_SUCCESS;
    }
    if (token.kind == hitsimple::lexer::TokenKind::Invalid) {
      auto diagnostic = hitsimple::diagnostic::Diagnostic::error(
          hitsimple::diagnostic::Stage::Lexer, "invalid token");
      diagnostic.range = token.range;
      if (!token.lexeme.empty()) {
        diagnostic.message += " `" + escapeLexeme(token.lexeme) + '`';
      }
      printDiagnostic(diagnostic);
      return EXIT_FAILURE;
    }
  }
}

int dumpAst(const std::string& inputPath, bool cCompatibilityMode) {
  auto parsed = parseInput(inputPath, cCompatibilityMode);
  if (!parsed) {
    return EXIT_FAILURE;
  }

  hitsimple::ast::dump(*parsed->unit, std::cout);
  return EXIT_SUCCESS;
}

int dumpHir(const std::string& inputPath, bool cCompatibilityMode,
            hitsimple::stdlib::BuiltinProviderSelection providerSelection) {
  auto parsed = parseInput(inputPath, cCompatibilityMode);
  if (!parsed) {
    return EXIT_FAILURE;
  }

  auto analyzeResult = hitsimple::sema::analyze(
      *parsed->unit,
      hitsimple::sema::AnalyzeOptions{false, parsed->standardHeaders,
                                      cCompatibilityMode});
  if (!analyzeResult.unit) {
    printDiagnostics(analyzeResult.diagnostics);
    return EXIT_FAILURE;
  }
  if (cCompatibilityMode &&
      !applyCCompatibilityMetadata(*analyzeResult.unit,
                                   parsed->compatibilityLinkage)) {
    return EXIT_FAILURE;
  }
  (void)hitsimple::stdlib::selectStandardLibraryProviders(*analyzeResult.unit,
                                                          providerSelection);

  hitsimple::hir::dump(*analyzeResult.unit, std::cout);
  return EXIT_SUCCESS;
}

int emitLlvm(const std::string& inputPath,
             const std::optional<std::string>& outputPath,
             hitsimple::codegen::CodegenOptions codegenOptions,
             bool cCompatibilityMode,
             hitsimple::stdlib::BuiltinProviderSelection providerSelection,
             hitsimple::support::CompilationMetrics& metrics) {
  auto compiled = compileTranslationUnit(inputPath, codegenOptions,
                                         cCompatibilityMode, providerSelection,
                                         false, metrics);
  if (!compiled) {
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> units;
  units.push_back(std::move(*compiled));
  const auto moduleIds = collectRequiredSourceModules(units);
  auto sourceModules = compileSourceModules(moduleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  units.insert(units.end(), std::make_move_iterator(sourceModules->begin()),
               std::make_move_iterator(sourceModules->end()));
  std::string mergeError;
  const auto llvmIr = mergeLlvmModules(units, mergeError);
  if (!llvmIr) {
    std::cerr << "hsc: cannot merge LLVM IR: " << mergeError << '\n';
    return EXIT_FAILURE;
  }

  if (!outputPath) {
    std::cout << *llvmIr;
    metrics.markSkipped(metrics.llvmIrWrite());
    metrics.markSkipped(metrics.clangBackendLink());
    return EXIT_SUCCESS;
  }

  const auto writeStarted = metrics.now();
  if (!validateOutputParent(*outputPath)) {
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  if (!writeFile(*outputPath, *llvmIr)) {
    std::cerr << "hsc: cannot write output file '" << *outputPath << "'\n";
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);
  metrics.markSkipped(metrics.clangBackendLink());

  return EXIT_SUCCESS;
}

class TemporaryDirectory final {
public:
  explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {}

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool writeOptimizedLlvmIr(std::string_view llvmIr,
                          const std::filesystem::path& llvmIrPath,
                          const NativeBackendOptions& backendOptions) {
  hitsimple::codegen::OptimizationPipelineOptions pipelineOptions;
  pipelineOptions.optimization = backendOptions.optimization;
  pipelineOptions.pgoMode = backendOptions.pgoMode;
  pipelineOptions.profilePath =
      hitsimple::support::pathToUtf8(backendOptions.profilePath);
  pipelineOptions.emitRemarks = backendOptions.optimizationRemarksPath.has_value();

  std::string pipelineError;
  const auto optimized = hitsimple::codegen::runOptimizationPipeline(
      llvmIr, pipelineOptions, pipelineError);
  if (!optimized) {
    std::cerr << "hsc: optimization pipeline failed: " << pipelineError << '\n';
    return false;
  }
  if (backendOptions.optimizationRemarksPath &&
      !appendOptimizationRemarks(*backendOptions.optimizationRemarksPath,
                                 optimized->remarks)) {
    return false;
  }

  const auto llvmIrPathText = hitsimple::support::pathToUtf8(llvmIrPath);
  if (!writeFile(llvmIrPathText, optimized->llvmIr)) {
    std::cerr << "hsc: cannot write temporary LLVM IR '" << llvmIrPathText
              << "'\n";
    return false;
  }
  return true;
}

bool emitObjectWithClang(
    std::string_view llvmIr, const std::filesystem::path& llvmIrPath,
    const std::filesystem::path& objectPath,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions) {
  if (!validateBackendClangCompatibility(clang) ||
      !writeOptimizedLlvmIr(llvmIr, llvmIrPath, backendOptions)) {
    return false;
  }
  std::vector<std::string> arguments{
      "-x", "ir", hitsimple::support::pathToUtf8(llvmIrPath), "-c", "-o",
      hitsimple::support::pathToUtf8(objectPath)};
  appendHitSimpleIrCodegenArguments(arguments, backendOptions);
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  if (!process.launched) {
    std::cerr << "hsc: cannot start Clang '"
              << hitsimple::support::pathToUtf8(*clang.path)
              << "': " << process.error << '\n';
    return false;
  }
  if (process.exitCode != 0) {
    std::cerr << "hsc: Clang failed while compiling LLVM IR to object (exit code "
              << process.exitCode << ")\n";
    return false;
  }
  return true;
}

struct CompiledObjectTranslationUnit {
  std::size_t mainDefinitionCount = 0;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<std::string> sourceModules;
};

std::optional<CompiledObjectTranslationUnit> compileObjectTranslationUnit(
    const std::string& inputPath, const std::filesystem::path& outputPath,
    const std::filesystem::path& llvmIrPath,
    hitsimple::codegen::CodegenOptions codegenOptions,
    bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    bool includeSourceModules,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  const auto metricsIndex = metrics.translationUnits().size();
  auto& unitMetrics = metrics.addTranslationUnit(inputPath);
  auto parsed = parseInput(inputPath, cCompatibilityMode, metrics, unitMetrics);
  if (!parsed) {
    return std::nullopt;
  }

  const auto mainCount = mainDefinitionCount(*parsed->unit);

  const auto semaStarted = metrics.now();
  auto analyzeResult = hitsimple::sema::analyze(
      *parsed->unit,
      hitsimple::sema::AnalyzeOptions{false, parsed->standardHeaders,
                                      cCompatibilityMode, false});
  if (!analyzeResult.unit) {
    printDiagnostics(analyzeResult.diagnostics);
    metrics.fail(unitMetrics.semaHir, semaStarted);
    metrics.fail("sema_hir");
    return std::nullopt;
  }
  const auto sourceModules = hitsimple::stdlib::selectStandardLibraryProviders(
      *analyzeResult.unit, providerSelection);
  if (cCompatibilityMode &&
      !applyCCompatibilityMetadata(*analyzeResult.unit,
                                   parsed->compatibilityLinkage)) {
    metrics.fail(unitMetrics.semaHir, semaStarted);
    metrics.fail("sema_hir");
    return std::nullopt;
  }
  unitMetrics.hirNodes =
      hitsimple::support::collectHirNodeMetrics(*analyzeResult.unit);
  metrics.complete(unitMetrics.semaHir, semaStarted);

  const auto emissionStarted = metrics.now();
  auto emitResult = hitsimple::codegen::emitLlvmIr(*analyzeResult.unit,
                                                    inputPath, codegenOptions);
  if (!emitResult.diagnostics.empty()) {
    printDiagnostics(emitResult.diagnostics);
    metrics.fail(unitMetrics.llvmEmission, emissionStarted);
    metrics.fail("llvm_emission");
    return std::nullopt;
  }
  std::string llvmIr = std::move(emitResult.llvmIr);
  if (includeSourceModules && !sourceModules.empty()) {
    std::vector<CompiledTranslationUnit> units;
    units.push_back(CompiledTranslationUnit{inputPath, std::move(llvmIr), mainCount,
                                            parsed->compatibilityLinkage,
                                            sourceModules});
    auto modules = compileSourceModules(sourceModules, codegenOptions, metrics);
    if (!modules) {
      return std::nullopt;
    }
    units.insert(units.end(), std::make_move_iterator(modules->begin()),
                 std::make_move_iterator(modules->end()));
    std::string mergeError;
    auto merged = mergeLlvmModules(units, mergeError);
    if (!merged) {
      std::cerr << "hsc: cannot merge LLVM IR: " << mergeError << '\n';
      return std::nullopt;
    }
    llvmIr = std::move(*merged);
  }
  auto& completedUnitMetrics = metrics.translationUnits().at(metricsIndex);
  completedUnitMetrics.llvmIrBytes = llvmIr.size();
  if (!emitObjectWithClang(llvmIr, llvmIrPath, outputPath, clang,
                           backendOptions)) {
    metrics.fail(completedUnitMetrics.llvmEmission, emissionStarted);
    metrics.fail("llvm_emission");
    return std::nullopt;
  }
  metrics.complete(completedUnitMetrics.llvmEmission, emissionStarted);
  return CompiledObjectTranslationUnit{mainCount,
                                       std::move(parsed->compatibilityLinkage),
                                       sourceModules};
}

std::string defaultObjectOutputPath(const std::string& inputPath) {
  auto output = hitsimple::support::pathFromUtf8(inputPath);
  output.replace_extension(".o");
  return hitsimple::support::pathToUtf8(output);
}

int compileObject(const std::vector<std::string>& inputPaths,
                  const std::optional<std::string>& outputPath,
                  hitsimple::codegen::CodegenOptions codegenOptions,
                  bool cCompatibilityMode,
                  hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                  const hitsimple::support::ClangSelection& clang,
                  const NativeBackendOptions& backendOptions,
                  hitsimple::support::CompilationMetrics& metrics) {
  if (inputPaths.size() != 1U) {
    std::cerr << "hsc: --crate-type=object supports exactly one input file\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const auto objectPath = outputPath.value_or(defaultObjectOutputPath(
      inputPaths.front()));
  if (!validateOutputParent(objectPath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-object-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create object temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);
  const auto writeStarted = metrics.now();
  if (!compileObjectTranslationUnit(
          inputPaths.front(), hitsimple::support::pathFromUtf8(objectPath),
          temporaryPath / "unit.ll", codegenOptions, cCompatibilityMode,
          providerSelection, true, clang, backendOptions, metrics)) {
    return EXIT_FAILURE;
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);
  metrics.markSkipped(metrics.clangBackendLink());
  return EXIT_SUCCESS;
}

int compileStaticLibrary(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const hitsimple::support::LlvmArSelection& llvmAr,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  if (!llvmAr.path) {
    std::cerr << "hsc: " << llvmAr.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const auto archivePath = outputPath.value_or("libhitsimple.a");
  if (!validateOutputParent(archivePath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-staticlib-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create static library temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);

  std::vector<std::filesystem::path> objectPaths;
  objectPaths.reserve(inputPaths.size());
  std::vector<std::string> sourceModuleIds;
  std::unordered_set<std::string> seenSourceModules;
  for (std::size_t index = 0; index < inputPaths.size(); ++index) {
    const auto objectPath = temporaryPath / ("unit-" +
        std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath,
        temporaryPath / ("unit-" + std::to_string(index) + ".ll"),
        codegenOptions, cCompatibilityMode, providerSelection, false, clang,
        backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    for (const auto& module : compiled->sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    objectPaths.push_back(objectPath);
  }
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath = temporaryPath /
        ("stdlib-" + std::to_string(index) + ".o");
    if (!emitObjectWithClang(
            (*sourceModules)[index].llvmIr,
            temporaryPath / ("stdlib-" + std::to_string(index) + ".ll"),
            objectPath, clang, backendOptions)) {
      metrics.fail("llvm_emission");
      return EXIT_FAILURE;
    }
    objectPaths.push_back(objectPath);
  }

  const auto runtimePath = hitsimple::support::runtimeLibraryPath();
  std::error_code runtimeError;
  if (!std::filesystem::is_regular_file(runtimePath, runtimeError)) {
    std::cerr << "hsc: runtime static library is unavailable '"
              << hitsimple::support::pathToUtf8(runtimePath) << "'";
    if (runtimeError) {
      std::cerr << ": " << runtimeError.message();
    }
    std::cerr << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto originalDirectory = std::filesystem::current_path(directoryError);
  if (directoryError) {
    std::cerr << "hsc: cannot read current directory while creating static library: "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  std::filesystem::current_path(temporaryPath, directoryError);
  if (directoryError) {
    std::cerr << "hsc: cannot enter static library temporary directory: "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto extractResult = hitsimple::support::runProcess(
      *llvmAr.path, {"x", hitsimple::support::pathToUtf8(runtimePath)});
  std::error_code restoreError;
  std::filesystem::current_path(originalDirectory, restoreError);
  if (restoreError) {
    std::cerr << "hsc: cannot restore current directory after llvm-ar: "
              << restoreError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!extractResult.launched) {
    std::cerr << "hsc: cannot start llvm-ar '"
              << hitsimple::support::pathToUtf8(*llvmAr.path) << "': "
              << extractResult.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (extractResult.exitCode != 0) {
    std::cerr << "hsc: llvm-ar failed while extracting the runtime archive "
              << "(exit code " << extractResult.exitCode << ")\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  for (const auto& entry : std::filesystem::directory_iterator(temporaryPath)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension();
    if (extension == ".o" || extension == ".obj") {
      if (std::find(objectPaths.begin(), objectPaths.end(), entry.path()) ==
          objectPaths.end()) {
        objectPaths.push_back(entry.path());
      }
    }
  }
  if (objectPaths.size() == inputPaths.size()) {
    std::cerr << "hsc: llvm-ar extracted no runtime object members\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto temporaryArchive = temporaryPath / "libhitsimple.a";
  std::vector<std::string> archiveArguments{"rcs",
      hitsimple::support::pathToUtf8(temporaryArchive)};
  for (const auto& objectPath : objectPaths) {
    archiveArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  const auto archiveResult = hitsimple::support::runProcess(*llvmAr.path,
                                                             archiveArguments);
  if (!archiveResult.launched) {
    std::cerr << "hsc: cannot start llvm-ar '"
              << hitsimple::support::pathToUtf8(*llvmAr.path) << "': "
              << archiveResult.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (archiveResult.exitCode != 0) {
    std::cerr << "hsc: llvm-ar failed while creating static library (exit code "
              << archiveResult.exitCode << ")\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  std::error_code copyError;
  std::filesystem::copy_file(temporaryArchive,
                             hitsimple::support::pathFromUtf8(archivePath),
                             std::filesystem::copy_options::overwrite_existing,
                             copyError);
  if (copyError) {
    std::cerr << "hsc: cannot write static library '" << archivePath
              << "': " << copyError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  metrics.markSkipped(metrics.llvmIrWrite());
  metrics.markSkipped(metrics.clangBackendLink());
  return EXIT_SUCCESS;
}

int preprocessOnly(const std::string& inputPath,
                   const std::optional<std::string>& outputPath) {
  auto result = hitsimple::preprocessor::preprocessFile(inputPath);
  if (printDiagnostics(result.diagnostics)) {
    return EXIT_FAILURE;
  }
  if (!outputPath) {
    std::cout << result.source;
    return EXIT_SUCCESS;
  }
  if (!validateOutputParent(*outputPath)) {
    return EXIT_FAILURE;
  }
  if (!writeFile(*outputPath, result.source)) {
    std::cerr << "hsc: cannot write output file '" << *outputPath << "'\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int compileExecutable(const std::vector<std::string>& inputPaths,
                      const std::optional<std::string>& outputPath,
                      hitsimple::codegen::CodegenOptions codegenOptions,
                      bool cCompatibilityMode,
                      hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                      const hitsimple::support::ClangSelection& clang,
                      const NativeBackendOptions& backendOptions,
                      hitsimple::support::CompilationMetrics& metrics) {
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    const auto linkStarted = metrics.now();
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::size_t mainCount = 0;
  for (const auto& inputPath : inputPaths) {
    auto compiled =
        compileTranslationUnit(inputPath, codegenOptions, cCompatibilityMode,
                               providerSelection, false, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
  }

  if (mainCount == 0) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define a main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (mainCount > 1) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define only one main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (!validateCCompatibilityExternalAbi(units)) {
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  const auto sourceModuleIds = collectRequiredSourceModules(units);
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions,
                                            metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  units.insert(units.end(), std::make_move_iterator(sourceModules->begin()),
               std::make_move_iterator(sourceModules->end()));

#ifdef _WIN32
  const std::string executablePath = outputPath.value_or("a.exe");
#else
  const std::string executablePath = outputPath.value_or("a.out");
#endif
  const auto writeStarted = metrics.now();
  if (!validateOutputParent(executablePath)) {
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  if (!validateBackendClangCompatibility(clang)) {
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const bool emitWindowsPdb =
      codegenOptions.emitDebugInfo && usesWindowsCodeView(codegenOptions);
  std::optional<std::filesystem::path> pdbPath;
  if (emitWindowsPdb) {
    pdbPath = pdbPathForExecutable(executablePath);
    std::error_code removeError;
    std::filesystem::remove(*pdbPath, removeError);
    if (removeError) {
      std::cerr << "hsc: cannot remove stale PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath)
                << "': " << removeError.message() << '\n';
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  std::vector<std::filesystem::path> temporaryPaths;
  temporaryPaths.reserve(units.size());
  std::vector<std::filesystem::path> clangInputPaths;
  clangInputPaths.reserve(units.size());
  const auto tempDirectory = std::filesystem::temp_directory_path();
  const auto tempPrefix =
      "hitsimple-" +
      std::to_string(hitsimple::support::currentProcessId());
  for (std::size_t index = 0; index < units.size(); ++index) {
    const auto tempPath =
        tempDirectory / (tempPrefix + "-" + std::to_string(index) + ".ll");
    if (!writeOptimizedLlvmIr(units[index].llvmIr, tempPath, backendOptions)) {
      for (const auto& path : temporaryPaths) {
        std::filesystem::remove(path);
      }
      metrics.fail(metrics.llvmIrWrite(), writeStarted);
      metrics.fail("llvm_ir_write");
      return EXIT_FAILURE;
    }
    temporaryPaths.push_back(tempPath);
    clangInputPaths.push_back(tempPath);
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);

  std::vector<std::string> arguments{"-x", "ir"};
  for (const auto& tempPath : clangInputPaths) {
    arguments.push_back(hitsimple::support::pathToUtf8(tempPath));
  }
  if (const auto runtimeSource =
          hitsimple::support::pathEnvironmentVariable(
              "HITSIMPLE_RUNTIME_SOURCE")) {
    arguments.insert(arguments.end(),
                     {"-x", "c",
                      hitsimple::support::pathToUtf8(*runtimeSource)});
  } else {
    arguments.insert(arguments.end(), {"-x", "none",
                                       hitsimple::support::pathToUtf8(
                                           hitsimple::support::runtimeLibraryPath())});
  }
  appendHitSimpleIrCodegenArguments(arguments, backendOptions);
#ifdef _WIN32
  arguments.push_back("-static-libgcc");
  arguments.push_back("-static-libstdc++");
#endif
#if defined(__APPLE__)
  arguments.push_back("-lc++");
#endif
  if (codegenOptions.emitDebugInfo) {
    arguments.push_back("-g");
  }
  if (emitWindowsPdb) {
    arguments.push_back("-gcodeview");
    arguments.push_back("-fuse-ld=lld");
    arguments.push_back("-Xlinker");
    arguments.push_back("--pdb=" + hitsimple::support::pathToUtf8(*pdbPath));
  }
  arguments.insert(arguments.end(), {"-lm", "-o", executablePath});
  const auto linkStarted = metrics.now();
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  for (const auto& tempPath : temporaryPaths) {
    std::filesystem::remove(tempPath);
  }
  if (!process.launched) {
    std::cerr << "hsc: cannot start Clang '"
              << hitsimple::support::pathToUtf8(*clang.path)
              << "': " << process.error << '\n';
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (process.exitCode != 0) {
    std::cerr << "hsc: Clang failed while linking executable (exit code "
                << process.exitCode << ")\n";
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (pdbPath) {
    std::error_code pdbError;
    if (!std::filesystem::is_regular_file(*pdbPath, pdbError)) {
      std::cerr << "hsc: debug build did not generate PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath) << "'";
      if (pdbError) {
        std::cerr << ": " << pdbError.message();
      }
      std::cerr << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  metrics.complete(metrics.clangBackendLink(), linkStarted);

  return EXIT_SUCCESS;
}

int compileMixedExecutable(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const std::optional<std::filesystem::path>& clangOverride,
    const std::optional<std::filesystem::path>& clangxxOverride,
    const ExternalBuildInputs& externalInputs,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  const bool linkOnlyExternalInputs = externalInputs.cSources.empty() &&
      externalInputs.cxxSources.empty() &&
      (!externalInputs.linkInputs.empty() || !externalInputs.libraries.empty());
  if (linkOnlyExternalInputs && !externalInputs.linkerLanguage) {
    std::cerr << "hsc: archive or object link inputs require "
                 "--linker-language=c|cxx\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::Native &&
      externalInputs.cSources.empty() && externalInputs.cxxSources.empty()) {
    std::cerr << "hsc: --entry=native requires a --c-source or --cxx-source "
                 "that defines main\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const bool useCxxLinker = externalInputs.linkerLanguage
      ? *externalInputs.linkerLanguage == LinkerLanguage::Cxx
      : !externalInputs.cxxSources.empty();
  const bool needsCCompiler = true;
  const bool needsCxxCompiler = !externalInputs.cxxSources.empty() ||
      useCxxLinker;
  const auto clang = hitsimple::support::resolveClang(clangOverride);
  const auto clangxx = hitsimple::support::resolveClangxx(clangxxOverride);
  if (needsCCompiler && !clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (needsCxxCompiler && !clangxx.path) {
    std::cerr << "hsc: " << clangxx.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (!validateBackendClangCompatibility(clang) ||
      (needsCxxCompiler && !validateBackendClangCompatibility(clangxx))) {
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }

  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-link-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create link temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);

  std::vector<CompiledObjectTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::vector<std::filesystem::path> hitSimpleObjects;
  hitSimpleObjects.reserve(inputPaths.size());
  std::vector<std::string> sourceModuleIds;
  std::unordered_set<std::string> seenSourceModules;
  std::size_t mainCount = 0;
  for (std::size_t index = 0; index < inputPaths.size(); ++index) {
    const auto objectPath = temporaryPath /
        ("hitsimple-" + std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath,
        temporaryPath / ("hitsimple-" + std::to_string(index) + ".ll"),
        codegenOptions, cCompatibilityMode, providerSelection, false, clang,
        backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
    for (const auto& module : units.back().sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    hitSimpleObjects.push_back(objectPath);
  }
  if (externalInputs.entryMode == EntryMode::HitSimple && mainCount == 0) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define a main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::HitSimple && mainCount > 1) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define only one main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::Native && mainCount != 0) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "--entry=native forbids a HitSimple main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> compatibilityUnits;
  compatibilityUnits.reserve(units.size());
  for (const auto& unit : units) {
    compatibilityUnits.push_back(
        {inputPaths[compatibilityUnits.size()], "", unit.mainDefinitionCount,
         unit.compatibilityLinkage, {}});
  }
  if (!validateCCompatibilityExternalAbi(compatibilityUnits)) {
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions,
                                            metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath = temporaryPath /
        ("hitsimple-stdlib-" + std::to_string(index) + ".o");
    if (!emitObjectWithClang(
            (*sourceModules)[index].llvmIr,
            temporaryPath / ("hitsimple-stdlib-" + std::to_string(index) +
                             ".ll"),
            objectPath, clang, backendOptions)) {
      metrics.fail("llvm_emission");
      return EXIT_FAILURE;
    }
    hitSimpleObjects.push_back(objectPath);
  }

#ifdef _WIN32
  const std::string executablePath = outputPath.value_or("a.exe");
#else
  const std::string executablePath = outputPath.value_or("a.out");
#endif
  const auto writeStarted = metrics.now();
  if (!validateOutputParent(executablePath)) {
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);

  std::vector<std::filesystem::path> externalObjects;
  externalObjects.reserve(externalInputs.cSources.size() +
                          externalInputs.cxxSources.size() + 1U);
  const auto compileExternalSource = [&](const std::string& source,
                                         std::string_view label,
                                         const hitsimple::support::ClangSelection& compiler,
                                         std::size_t index) -> bool {
    const auto objectPath = temporaryPath /
        (std::string(label) + "-" + std::to_string(index) + ".o");
    std::vector<std::string> arguments{"-c", source, "-o",
                                        hitsimple::support::pathToUtf8(objectPath)};
    appendClangCodegenArguments(arguments, backendOptions);
    if (codegenOptions.emitDebugInfo) {
      arguments.push_back("-g");
    }
    const auto process = hitsimple::support::runProcess(*compiler.path, arguments);
    if (!process.launched) {
      std::cerr << "hsc: cannot start " << label << " compiler '"
                << hitsimple::support::pathToUtf8(*compiler.path) << "': "
                << process.error << '\n';
      return false;
    }
    if (process.exitCode != 0) {
      std::cerr << "hsc: " << label << " compiler failed for '" << source
                << "' (exit code " << process.exitCode << ")\n";
      return false;
    }
    externalObjects.push_back(objectPath);
    return true;
  };
  for (std::size_t index = 0; index < externalInputs.cSources.size(); ++index) {
    if (!compileExternalSource(externalInputs.cSources[index], "C", clang,
                               index)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  for (std::size_t index = 0; index < externalInputs.cxxSources.size(); ++index) {
    if (!compileExternalSource(externalInputs.cxxSources[index], "C++", clangxx,
                               index)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  std::optional<std::filesystem::path> runtimeObject;
  if (const auto runtimeSource = hitsimple::support::pathEnvironmentVariable(
          "HITSIMPLE_RUNTIME_SOURCE")) {
    if (!compileExternalSource(hitsimple::support::pathToUtf8(*runtimeSource),
                               "runtime C", clang, 0U)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
    runtimeObject = externalObjects.back();
    externalObjects.pop_back();
  }

  const bool emitWindowsPdb =
      codegenOptions.emitDebugInfo && usesWindowsCodeView(codegenOptions);
  std::optional<std::filesystem::path> pdbPath;
  if (emitWindowsPdb) {
    pdbPath = pdbPathForExecutable(executablePath);
    std::error_code removeError;
    std::filesystem::remove(*pdbPath, removeError);
    if (removeError) {
      std::cerr << "hsc: cannot remove stale PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath)
                << "': " << removeError.message() << '\n';
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }

  std::vector<std::string> linkArguments;
  for (const auto& objectPath : hitSimpleObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  for (const auto& objectPath : externalObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  linkArguments.insert(linkArguments.end(), externalInputs.linkInputs.begin(),
                       externalInputs.linkInputs.end());
  for (const auto& directory : externalInputs.libraryDirectories) {
    linkArguments.insert(linkArguments.end(), {"-L", directory});
  }
  for (const auto& library : externalInputs.libraries) {
    linkArguments.insert(linkArguments.end(), {"-l", library});
  }
  linkArguments.insert(linkArguments.end(), externalInputs.linkArguments.begin(),
                       externalInputs.linkArguments.end());
  if (runtimeObject) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(*runtimeObject));
  } else {
    linkArguments.push_back(hitsimple::support::pathToUtf8(
        hitsimple::support::runtimeLibraryPath()));
  }
  appendClangLinkArguments(linkArguments, backendOptions);
#ifdef _WIN32
  linkArguments.push_back("--target=x86_64-w64-windows-gnu");
  linkArguments.push_back("-static-libgcc");
  linkArguments.push_back("-static-libstdc++");
#endif
#if defined(__APPLE__)
  linkArguments.push_back("-lc++");
#endif
  if (codegenOptions.emitDebugInfo) {
    linkArguments.push_back("-g");
  }
  if (emitWindowsPdb) {
    linkArguments.push_back("-gcodeview");
    linkArguments.push_back("-fuse-ld=lld");
    linkArguments.push_back("-Xlinker");
    linkArguments.push_back("--pdb=" +
                            hitsimple::support::pathToUtf8(*pdbPath));
  }
  linkArguments.insert(linkArguments.end(), {"-lm", "-o", executablePath});
  const auto linkStarted = metrics.now();
  const auto& linker = useCxxLinker ? clangxx : clang;
  const auto linkResult =
      hitsimple::support::runProcess(*linker.path, linkArguments);
  if (!linkResult.launched) {
    std::cerr << "hsc: cannot start linker '"
              << hitsimple::support::pathToUtf8(*linker.path) << "': "
              << linkResult.error << '\n';
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (linkResult.exitCode != 0) {
    std::cerr << "hsc: linker failed while linking executable (exit code "
              << linkResult.exitCode << ")\n";
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (pdbPath) {
    std::error_code pdbError;
    if (!std::filesystem::is_regular_file(*pdbPath, pdbError)) {
      std::cerr << "hsc: debug build did not generate PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath) << "'";
      if (pdbError) {
        std::cerr << ": " << pdbError.message();
      }
      std::cerr << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  metrics.complete(metrics.clangBackendLink(), linkStarted);
  return EXIT_SUCCESS;
}

} // namespace

int hscMain(const std::vector<std::string>& arguments) {
  if (arguments.size() == 1U) {
    std::cerr << "hsc: missing input file\n";
    return EXIT_FAILURE;
  }

  bool shouldDumpTokens = false;
  bool shouldDumpAst = false;
  bool shouldDumpHir = false;
  bool shouldEmitLlvm = false;
  bool shouldPreprocessOnly = false;
  bool shouldPrintTargetInfo = false;
  bool cCompatibilityMode = false;
  bool shouldPrintTiming = false;
  auto requestedDiagnosticFormat = DiagnosticOutputFormat::Human;
  CrateType crateType = CrateType::Bin;
  bool crateTypeSelected = false;
  auto providerSelection =
      hitsimple::stdlib::BuiltinProviderSelection::Optimized;
  hitsimple::support::CompilationMetrics metrics;
  hitsimple::codegen::CodegenOptions codegenOptions;
  codegenOptions.targetTriple = targetTriple();
  NativeBackendOptions backendOptions;
  std::vector<std::string> inputPaths;
  std::optional<std::string> outputPath;
  std::optional<std::string> timingJsonPath;
  std::optional<std::filesystem::path> clangOverride;
  std::optional<std::filesystem::path> clangxxOverride;
  ExternalBuildInputs externalInputs;
  std::optional<std::filesystem::path> cargoManifest;
  std::optional<std::string> cargoPackage;
  std::optional<std::string> cargoProfile;
  std::optional<std::string> cargoFeatures;
  bool cargoNoDefaultFeatures = false;

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

  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string_view arg(arguments[i]);

    if (arg == "-h" || arg == "--help") {
      printHelp(std::cout);
      return finish(EXIT_SUCCESS);
    }

    if (arg == "--version") {
      printVersion(std::cout);
      return finish(EXIT_SUCCESS);
    }

    if (arg == "--target-info") {
      shouldPrintTargetInfo = true;
      continue;
    }

    if (arg == "--dump-tokens") {
      shouldDumpTokens = true;
      continue;
    }

    if (arg == "--dump-ast") {
      shouldDumpAst = true;
      continue;
    }

    if (arg == "--dump-hir") {
      shouldDumpHir = true;
      continue;
    }

    if (arg == "--emit-llvm") {
      shouldEmitLlvm = true;
      continue;
    }

    if (arg == "--emit-object") {
      if (crateTypeSelected && crateType != CrateType::Object) {
        std::cerr << "hsc: --emit-object conflicts with --crate-type\n";
        return finish(EXIT_FAILURE);
      }
      crateType = CrateType::Object;
      crateTypeSelected = true;
      continue;
    }

    if (arg.starts_with("--crate-type=")) {
      const auto value = arg.substr(std::string_view("--crate-type=").size());
      CrateType selected;
      if (value == "bin") {
        selected = CrateType::Bin;
      } else if (value == "object") {
        selected = CrateType::Object;
      } else if (value == "staticlib") {
        selected = CrateType::StaticLib;
      } else {
        std::cerr << "hsc: unsupported --crate-type '" << value
                  << "'; expected bin, object, or staticlib\n";
        return finish(EXIT_FAILURE);
      }
      if (crateTypeSelected && crateType != selected) {
        std::cerr << "hsc: conflicting --crate-type options\n";
        return finish(EXIT_FAILURE);
      }
      crateType = selected;
      crateTypeSelected = true;
      continue;
    }

    if (arg == "--crate-type") {
      std::cerr << "hsc: --crate-type requires --crate-type=<bin|object|staticlib>\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "-O0") {
      backendOptions.optimization = OptimizationLevel::O0;
      continue;
    }

    if (arg == "-O1") {
      backendOptions.optimization = OptimizationLevel::O1;
      continue;
    }

    if (arg == "-O2") {
      backendOptions.optimization = OptimizationLevel::O2;
      continue;
    }

    if (arg == "-O3") {
      backendOptions.optimization = OptimizationLevel::O3;
      continue;
    }

    if (arg == "-Os") {
      backendOptions.optimization = OptimizationLevel::Os;
      continue;
    }

    if (arg.starts_with("--optimization-remarks=")) {
      const auto path =
          arg.substr(std::string_view("--optimization-remarks=").size());
      if (path.empty()) {
        std::cerr << "hsc: --optimization-remarks requires a file path\n";
        return finish(EXIT_FAILURE);
      }
      if (backendOptions.optimizationRemarksPath) {
        std::cerr << "hsc: --optimization-remarks may only be specified once\n";
        return finish(EXIT_FAILURE);
      }
      backendOptions.optimizationRemarksPath =
          hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--optimization-remarks") {
      std::cerr << "hsc: --optimization-remarks requires "
                   "--optimization-remarks=<path>\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "-g") {
      codegenOptions.emitDebugInfo = true;
      continue;
    }

    if (arg.starts_with("--pgo-instrument=")) {
      const auto path =
          arg.substr(std::string_view("--pgo-instrument=").size());
      if (path.empty()) {
        std::cerr << "hsc: --pgo-instrument requires a raw profile path\n";
        return finish(EXIT_FAILURE);
      }
      if (backendOptions.pgoMode != PgoMode::None) {
        if (backendOptions.pgoMode == PgoMode::Instrument) {
          std::cerr << "hsc: --pgo-instrument may only be specified once\n";
        } else {
          std::cerr << "hsc: --pgo-instrument and --pgo-use are mutually exclusive\n";
        }
        return finish(EXIT_FAILURE);
      }
      backendOptions.pgoMode = PgoMode::Instrument;
      backendOptions.profilePath = hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--pgo-instrument") {
      std::cerr << "hsc: --pgo-instrument requires --pgo-instrument=<profraw>\n";
      return finish(EXIT_FAILURE);
    }

    if (arg.starts_with("--pgo-use=")) {
      const auto path = arg.substr(std::string_view("--pgo-use=").size());
      if (path.empty()) {
        std::cerr << "hsc: --pgo-use requires a merged profile path\n";
        return finish(EXIT_FAILURE);
      }
      if (backendOptions.pgoMode != PgoMode::None) {
        if (backendOptions.pgoMode == PgoMode::Use) {
          std::cerr << "hsc: --pgo-use may only be specified once\n";
        } else {
          std::cerr << "hsc: --pgo-instrument and --pgo-use are mutually exclusive\n";
        }
        return finish(EXIT_FAILURE);
      }
      backendOptions.pgoMode = PgoMode::Use;
      backendOptions.profilePath = hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--pgo-use") {
      std::cerr << "hsc: --pgo-use requires --pgo-use=<profdata>\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "--timing") {
      shouldPrintTiming = true;
      continue;
    }

    if (arg.starts_with("--timing-json=")) {
      const auto path = arg.substr(std::string_view("--timing-json=").size());
      if (path.empty()) {
        std::cerr << "hsc: --timing-json requires a file path\n";
        return finish(EXIT_FAILURE);
      }
      timingJsonPath = std::string(path);
      continue;
    }

    if (arg == "--diagnostic-format=json") {
      requestedDiagnosticFormat = DiagnosticOutputFormat::Json;
      continue;
    }

    if (arg.starts_with("--diagnostic-format=")) {
      std::cerr << "hsc: unsupported --diagnostic-format '"
                << arg.substr(std::string_view("--diagnostic-format=").size())
                << "'; expected json\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "--diagnostic-format") {
      std::cerr << "hsc: --diagnostic-format requires "
                   "--diagnostic-format=json\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "--timing-json") {
      std::cerr << "hsc: --timing-json requires --timing-json=<path>\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "--c-compat") {
      cCompatibilityMode = true;
      continue;
    }

    if (arg.starts_with("--stdlib-provider=")) {
      const auto value =
          arg.substr(std::string_view("--stdlib-provider=").size());
      if (value == "optimized") {
        providerSelection = hitsimple::stdlib::BuiltinProviderSelection::Optimized;
      } else if (value == "reference") {
        providerSelection = hitsimple::stdlib::BuiltinProviderSelection::Reference;
      } else {
        std::cerr << "hsc: unsupported --stdlib-provider '" << value
                  << "'; expected optimized or reference\n";
        return finish(EXIT_FAILURE);
      }
      continue;
    }

    if (arg == "--stdlib-provider") {
      std::cerr << "hsc: --stdlib-provider requires "
                   "--stdlib-provider=optimized|reference\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "-E" || arg == "--preprocess-only") {
      shouldPreprocessOnly = true;
      continue;
    }

    if (arg == "--checked") {
      codegenOptions.safetyMode = hitsimple::codegen::SafetyMode::Checked;
      continue;
    }

    if (arg == "--static-checked") {
      codegenOptions.safetyMode =
          hitsimple::codegen::SafetyMode::StaticChecked;
      continue;
    }

    if (arg == "--unchecked") {
      codegenOptions.safetyMode = hitsimple::codegen::SafetyMode::Unchecked;
      continue;
    }

    if (arg == "--clang") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --clang requires an executable path\n";
        return finish(EXIT_FAILURE);
      }
      ++i;
      clangOverride = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--clangxx") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --clangxx requires an executable path\n";
        return finish(EXIT_FAILURE);
      }
      ++i;
      clangxxOverride = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--c-source") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --c-source requires a source path\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.cSources.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--cxx-source") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --cxx-source requires a source path\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.cxxSources.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--link-input") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --link-input requires a path\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.linkInputs.push_back(arguments[++i]);
      continue;
    }

    if (arg == "-L") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: -L requires a directory\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.libraryDirectories.push_back(arguments[++i]);
      continue;
    }

    if (arg == "-l") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: -l requires a library name\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.libraries.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--link-arg") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --link-arg requires an argument\n";
        return finish(EXIT_FAILURE);
      }
      externalInputs.linkArguments.push_back(arguments[++i]);
      continue;
    }

    if (arg.starts_with("--entry=")) {
      const auto value = arg.substr(std::string_view("--entry=").size());
      if (value == "hsc") {
        externalInputs.entryMode = EntryMode::HitSimple;
      } else if (value == "native") {
        externalInputs.entryMode = EntryMode::Native;
      } else {
        std::cerr << "hsc: unsupported --entry '" << value
                  << "'; expected hsc or native\n";
        return finish(EXIT_FAILURE);
      }
      continue;
    }

    if (arg == "--entry") {
      std::cerr << "hsc: --entry requires --entry=hsc|native\n";
      return finish(EXIT_FAILURE);
    }

    if (arg.starts_with("--linker-language=")) {
      const auto value =
          arg.substr(std::string_view("--linker-language=").size());
      if (value == "c") {
        externalInputs.linkerLanguage = LinkerLanguage::C;
      } else if (value == "cxx") {
        externalInputs.linkerLanguage = LinkerLanguage::Cxx;
      } else {
        std::cerr << "hsc: unsupported --linker-language '" << value
                  << "'; expected c or cxx\n";
        return finish(EXIT_FAILURE);
      }
      continue;
    }

    if (arg == "--linker-language") {
      std::cerr << "hsc: --linker-language requires "
                   "--linker-language=c|cxx\n";
      return finish(EXIT_FAILURE);
    }

    if (arg == "--cargo-manifest") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --cargo-manifest requires a Cargo.toml path\n";
        return finish(EXIT_FAILURE);
      }
      ++i;
      cargoManifest = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--cargo-package") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --cargo-package requires a package name\n";
        return finish(EXIT_FAILURE);
      }
      cargoPackage = arguments[++i];
      continue;
    }

    if (arg == "--cargo-profile") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --cargo-profile requires a profile name\n";
        return finish(EXIT_FAILURE);
      }
      cargoProfile = arguments[++i];
      continue;
    }

    if (arg == "--cargo-features") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: --cargo-features requires a feature list\n";
        return finish(EXIT_FAILURE);
      }
      cargoFeatures = arguments[++i];
      continue;
    }

    if (arg == "--cargo-no-default-features") {
      cargoNoDefaultFeatures = true;
      continue;
    }

    if (arg == "-o") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: -o requires an output path\n";
        return finish(EXIT_FAILURE);
      }
      ++i;
      outputPath = arguments[i];
      continue;
    }

    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "hsc: unknown option '" << arg << "'\n";
      return finish(EXIT_FAILURE);
    }

    inputPaths.push_back(std::string(arg));
  }

  const DiagnosticOutputFormatScope diagnosticFormatScope(
      requestedDiagnosticFormat);

  std::vector<std::string_view> actions;
  if (shouldDumpTokens) {
    if (cCompatibilityMode) {
      std::cerr << "hsc: --dump-tokens is not supported with --c-compat\n";
      return finish(EXIT_FAILURE);
    }
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
    if (cCompatibilityMode) {
      std::cerr << "hsc: --c-compat is not supported with --target-info\n";
      return finish(EXIT_FAILURE);
    }
    actions.push_back("--target-info");
  }
  if (actions.size() > 1) {
    std::cerr << "hsc: multiple action options are not allowed:";
    for (const auto action : actions) {
      std::cerr << ' ' << action;
    }
    std::cerr << '\n';
    return finish(EXIT_FAILURE);
  }

  if (backendOptions.pgoMode != PgoMode::None &&
      crateType != CrateType::Bin) {
    std::cerr << "hsc: PGO options are only supported for --crate-type=bin\n";
    return finish(EXIT_FAILURE);
  }
  if (backendOptions.pgoMode != PgoMode::None && !actions.empty()) {
    if (shouldEmitLlvm) {
      std::cerr << "hsc: PGO options are not supported with --emit-llvm\n";
    } else {
      std::cerr << "hsc: PGO options are only supported for executable builds\n";
    }
    return finish(EXIT_FAILURE);
  }
  if (backendOptions.optimizationRemarksPath && !actions.empty()) {
    std::cerr << "hsc: --optimization-remarks is only supported for native "
                 "code generation\n";
    return finish(EXIT_FAILURE);
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
  const bool hasCargoBuildOptions = cargoManifest.has_value() ||
      cargoPackage.has_value() || cargoProfile.has_value() ||
      cargoFeatures.has_value() || cargoNoDefaultFeatures;
  if (hasCargoBuildOptions && !cargoManifest) {
    std::cerr << "hsc: Cargo options require --cargo-manifest <Cargo.toml>\n";
    return finish(EXIT_FAILURE);
  }

  const bool hasMixedBuildOptions = externalInputs.hasMixedBuildOptions() ||
      clangxxOverride.has_value() || hasCargoBuildOptions;
  if (hasMixedBuildOptions && !actions.empty()) {
    std::cerr << "hsc: C/C++ source, Cargo, and native linker options are only "
                 "supported for executable builds\n";
    return finish(EXIT_FAILURE);
  }

  if (crateType != CrateType::Bin && !actions.empty()) {
    std::cerr << "hsc: --crate-type is not supported with " << actions.front()
              << '\n';
    return finish(EXIT_FAILURE);
  }

  if (crateType != CrateType::Bin && hasMixedBuildOptions) {
    std::cerr << "hsc: C/C++ source, Cargo, and native linker options are not "
                 "supported with --crate-type\n";
    return finish(EXIT_FAILURE);
  }

  if (clangxxOverride && externalInputs.cxxSources.empty() &&
      (!externalInputs.linkerLanguage ||
       *externalInputs.linkerLanguage != LinkerLanguage::Cxx)) {
    std::cerr << "hsc: --clangxx requires --cxx-source or "
                 "--linker-language=cxx\n";
    return finish(EXIT_FAILURE);
  }

  if (codegenOptions.emitDebugInfo && !(shouldEmitLlvm || actions.empty())) {
    std::cerr << "hsc: -g is only supported for executable builds and --emit-llvm\n";
    return finish(EXIT_FAILURE);
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

#ifdef _WIN32
std::string utf8Argument(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    arguments.push_back(utf8Argument(argv[index]));
  }
  return hscMain(arguments);
}
#else
int main(int argc, char** argv) {
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }
  return hscMain(arguments);
}
#endif
