#include "hitsimple/lexer/Lexer.h"
#include "hitsimple/ast/AST.h"
#include "hitsimple/compat/CCompat.h"
#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/codegen/TargetCapabilities.h"
#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/sema/Sema.h"
#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"
#include "hitsimple/support/Toolchain.h"

#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

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
      << "  -g                Emit DWARF debug information\n"
      << "  --c-compat        Parse all inputs as C compatibility source\n"
      << "  -E, --preprocess-only\n"
      << "                   Print preprocessed source\n"
      << "  --target-info     Print implementation-defined target information\n"
      << "  --checked         Enable static and runtime safety checks\n"
      << "  --static-checked  Enable static safety diagnostics without runtime checks\n"
      << "  --unchecked       Disable safety checks (default)\n"
      << "  --clang <path>    Use a specific Clang executable for linking\n"
      << "  -o <path>         Write output to <path>\n";
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
      << "static-checked.runtime-overhead: none\n"
      << "abi.symbol.names: source identifiers use external linkage as written\n"
      << "abi.main: unannotated main uses i32, fallthrough returns 0\n"
      << "build.translation_units: each input is preprocessed, parsed, analyzed, "
         "and emitted independently; LLVM modules and runtime link once\n"
      << "build.cross_tu: matching extern declarations are required; C "
         "compatibility validates external ABI shapes before link; typedefs, "
         "macro expansions, and file-scope static remain TU-local\n"
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

bool printDiagnostics(const std::vector<hitsimple::diagnostic::Diagnostic>& diagnostics) {
  bool hasError = false;
  for (const auto& diagnostic : diagnostics) {
    std::cerr << "hsc: " << diagnostic << '\n';
    hasError = hasError ||
               diagnostic.severity == hitsimple::diagnostic::Severity::Error;
  }
  return hasError;
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

std::optional<std::string> readFile(const std::string& path) {
  std::ifstream input(hitsimple::support::pathFromUtf8(path),
                      std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
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
  std::string llvmIr;
  std::size_t mainDefinitionCount = 0;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
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
  std::unordered_map<std::string,
                     const hitsimple::compat::LinkageMetadata*> declarations;
  for (const auto& unit : units) {
    for (const auto& item : unit.compatibilityLinkage) {
      if (item.linkage != hitsimple::compat::Linkage::External) {
        continue;
      }
      const auto [found, inserted] =
          declarations.emplace(item.coreName, &item);
      if (inserted || sameCExternalAbi(*found->second, item)) {
        continue;
      }
      const auto& name = item.sourceName.empty() ? item.coreName : item.sourceName;
      std::cerr << "hsc: incompatible C external declaration '" << name
                << "' across translation units\n";
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
    bool cCompatibilityMode) {
  auto parsed = parseInput(inputPath, cCompatibilityMode);
  if (!parsed) {
    return std::nullopt;
  }

  const auto mainCount = mainDefinitionCount(*parsed->unit);
  auto analyzeResult = hitsimple::sema::analyze(
      *parsed->unit,
      hitsimple::sema::AnalyzeOptions{false, parsed->standardHeaders,
                                      cCompatibilityMode});
  if (!analyzeResult.unit) {
    for (const auto& diagnostic : analyzeResult.diagnostics) {
      std::cerr << "hsc: " << diagnostic << '\n';
    }
    return std::nullopt;
  }
  if (cCompatibilityMode &&
      !applyCCompatibilityMetadata(*analyzeResult.unit,
                                   parsed->compatibilityLinkage)) {
    return std::nullopt;
  }

  auto emitResult =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, inputPath,
                                     codegenOptions);
  if (!emitResult.diagnostics.empty()) {
    for (const auto& diagnostic : emitResult.diagnostics) {
      std::cerr << "hsc: " << diagnostic << '\n';
    }
    return std::nullopt;
  }

  return CompiledTranslationUnit{std::move(emitResult.llvmIr), mainCount,
                                 std::move(parsed->compatibilityLinkage)};
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
      std::cerr << "hsc: " << diagnostic << '\n';
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

int dumpHir(const std::string& inputPath, bool cCompatibilityMode) {
  auto parsed = parseInput(inputPath, cCompatibilityMode);
  if (!parsed) {
    return EXIT_FAILURE;
  }

  auto analyzeResult = hitsimple::sema::analyze(
      *parsed->unit,
      hitsimple::sema::AnalyzeOptions{false, parsed->standardHeaders,
                                      cCompatibilityMode});
  if (!analyzeResult.unit) {
    for (const auto& diagnostic : analyzeResult.diagnostics) {
      std::cerr << "hsc: " << diagnostic << '\n';
    }
    return EXIT_FAILURE;
  }
  if (cCompatibilityMode &&
      !applyCCompatibilityMetadata(*analyzeResult.unit,
                                   parsed->compatibilityLinkage)) {
    return EXIT_FAILURE;
  }

  hitsimple::hir::dump(*analyzeResult.unit, std::cout);
  return EXIT_SUCCESS;
}

int emitLlvm(const std::string& inputPath,
             const std::optional<std::string>& outputPath,
             hitsimple::codegen::CodegenOptions codegenOptions,
             bool cCompatibilityMode) {
  auto compiled =
      compileTranslationUnit(inputPath, codegenOptions, cCompatibilityMode);
  if (!compiled) {
    return EXIT_FAILURE;
  }

  if (!outputPath) {
    std::cout << compiled->llvmIr;
    return EXIT_SUCCESS;
  }

  if (!validateOutputParent(*outputPath)) {
    return EXIT_FAILURE;
  }
  if (!writeFile(*outputPath, compiled->llvmIr)) {
    std::cerr << "hsc: cannot write output file '" << *outputPath << "'\n";
    return EXIT_FAILURE;
  }

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
                      const hitsimple::support::ClangSelection& clang) {
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::size_t mainCount = 0;
  for (const auto& inputPath : inputPaths) {
    auto compiled =
        compileTranslationUnit(inputPath, codegenOptions, cCompatibilityMode);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
  }

  if (mainCount == 0) {
    std::cerr << "hsc: "
              << hitsimple::diagnostic::Diagnostic::error(
                     hitsimple::diagnostic::Stage::Sema,
                     "program must define a main function")
              << '\n';
    return EXIT_FAILURE;
  }
  if (mainCount > 1) {
    std::cerr << "hsc: "
              << hitsimple::diagnostic::Diagnostic::error(
                     hitsimple::diagnostic::Stage::Sema,
                     "program must define only one main function")
              << '\n';
    return EXIT_FAILURE;
  }
  if (!validateCCompatibilityExternalAbi(units)) {
    return EXIT_FAILURE;
  }

#ifdef _WIN32
  const std::string executablePath = outputPath.value_or("a.exe");
#else
  const std::string executablePath = outputPath.value_or("a.out");
#endif
  if (!validateOutputParent(executablePath)) {
    return EXIT_FAILURE;
  }
  std::vector<std::filesystem::path> tempPaths;
  tempPaths.reserve(units.size());
  const auto tempDirectory = std::filesystem::temp_directory_path();
  const auto tempPrefix =
      "hitsimple-" +
      std::to_string(hitsimple::support::currentProcessId());
  for (std::size_t index = 0; index < units.size(); ++index) {
    const auto tempPath =
        tempDirectory / (tempPrefix + "-" + std::to_string(index) + ".ll");
    const auto tempPathText = hitsimple::support::pathToUtf8(tempPath);
    if (!writeFile(tempPathText, units[index].llvmIr)) {
      std::cerr << "hsc: cannot write temporary file '" << tempPathText
                << "'\n";
      for (const auto& path : tempPaths) {
        std::filesystem::remove(path);
      }
      return EXIT_FAILURE;
    }
    tempPaths.push_back(tempPath);
  }

  std::vector<std::string> arguments{"-x", "ir"};
  for (const auto& tempPath : tempPaths) {
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
#ifdef _WIN32
  arguments.push_back("--target=x86_64-w64-windows-gnu");
  arguments.push_back("-static-libgcc");
  arguments.push_back("-static-libstdc++");
#endif
#if defined(__APPLE__)
  arguments.push_back("-lc++");
#endif
  if (codegenOptions.emitDebugInfo) {
    arguments.push_back("-g");
  }
  arguments.insert(arguments.end(), {"-lm", "-o", executablePath});
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  for (const auto& tempPath : tempPaths) {
    std::filesystem::remove(tempPath);
  }
  if (!process.launched) {
    std::cerr << "hsc: cannot start Clang '"
              << hitsimple::support::pathToUtf8(*clang.path)
              << "': " << process.error << '\n';
    return EXIT_FAILURE;
  }
  if (process.exitCode != 0) {
    std::cerr << "hsc: Clang failed while linking executable (exit code "
              << process.exitCode << ")\n";
    return EXIT_FAILURE;
  }

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
  hitsimple::codegen::CodegenOptions codegenOptions;
  codegenOptions.targetTriple = targetTriple();
  std::vector<std::string> inputPaths;
  std::optional<std::string> outputPath;
  std::optional<std::filesystem::path> clangOverride;

  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string_view arg(arguments[i]);

    if (arg == "-h" || arg == "--help") {
      printHelp(std::cout);
      return EXIT_SUCCESS;
    }

    if (arg == "--version") {
      printVersion(std::cout);
      return EXIT_SUCCESS;
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

    if (arg == "-g") {
      codegenOptions.emitDebugInfo = true;
      continue;
    }

    if (arg == "--c-compat") {
      cCompatibilityMode = true;
      continue;
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
        return EXIT_FAILURE;
      }
      ++i;
      clangOverride = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "-o") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "hsc: -o requires an output path\n";
        return EXIT_FAILURE;
      }
      ++i;
      outputPath = arguments[i];
      continue;
    }

    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "hsc: unknown option '" << arg << "'\n";
      return EXIT_FAILURE;
    }

    inputPaths.push_back(std::string(arg));
  }

  std::vector<std::string_view> actions;
  if (shouldDumpTokens) {
    if (cCompatibilityMode) {
      std::cerr << "hsc: --dump-tokens is not supported with --c-compat\n";
      return EXIT_FAILURE;
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
      return EXIT_FAILURE;
    }
    actions.push_back("--target-info");
  }
  if (actions.size() > 1) {
    std::cerr << "hsc: multiple action options are not allowed:";
    for (const auto action : actions) {
      std::cerr << ' ' << action;
    }
    std::cerr << '\n';
    return EXIT_FAILURE;
  }

  if (codegenOptions.emitDebugInfo && !(shouldEmitLlvm || actions.empty())) {
    std::cerr << "hsc: -g is only supported for executable builds and --emit-llvm\n";
    return EXIT_FAILURE;
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
      return EXIT_FAILURE;
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-tokens requires an input file\n";
      return EXIT_FAILURE;
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-tokens supports exactly one input file\n";
      return EXIT_FAILURE;
    }
    return dumpTokens(inputPaths.front());
  }

  if (shouldPrintTargetInfo) {
    if (rejectOutputPath("--target-info")) {
      return EXIT_FAILURE;
    }
    if (!inputPaths.empty()) {
      std::cerr << "hsc: --target-info does not take an input file\n";
      return EXIT_FAILURE;
    }
    printTargetInfo(std::cout, hitsimple::support::resolveClang(clangOverride));
    return EXIT_SUCCESS;
  }

  if (shouldDumpAst) {
    if (rejectOutputPath("--dump-ast")) {
      return EXIT_FAILURE;
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-ast requires an input file\n";
      return EXIT_FAILURE;
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-ast supports exactly one input file\n";
      return EXIT_FAILURE;
    }
    return dumpAst(inputPaths.front(), cCompatibilityMode);
  }

  if (shouldDumpHir) {
    if (rejectOutputPath("--dump-hir")) {
      return EXIT_FAILURE;
    }
    if (inputPaths.empty()) {
      std::cerr << "hsc: --dump-hir requires an input file\n";
      return EXIT_FAILURE;
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --dump-hir supports exactly one input file\n";
      return EXIT_FAILURE;
    }
    return dumpHir(inputPaths.front(), cCompatibilityMode);
  }

  if (shouldEmitLlvm) {
    if (inputPaths.empty()) {
      std::cerr << "hsc: --emit-llvm requires an input file\n";
      return EXIT_FAILURE;
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --emit-llvm supports exactly one input file\n";
      return EXIT_FAILURE;
    }
    return emitLlvm(inputPaths.front(), outputPath, codegenOptions,
                    cCompatibilityMode);
  }

  if (shouldPreprocessOnly) {
    if (inputPaths.empty()) {
      std::cerr << "hsc: --preprocess-only requires an input file\n";
      return EXIT_FAILURE;
    }
    if (inputPaths.size() > 1U) {
      std::cerr << "hsc: --preprocess-only supports exactly one input file\n";
      return EXIT_FAILURE;
    }
    return preprocessOnly(inputPaths.front(), outputPath);
  }

  if (inputPaths.empty()) {
    std::cerr << "hsc: missing input file\n";
    return EXIT_FAILURE;
  }

  return compileExecutable(inputPaths, outputPath, codegenOptions,
                           cCompatibilityMode,
                           hitsimple::support::resolveClang(clangOverride));
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
