#include "driver/FrontendPipeline.h"

#include "driver/Diagnostics.h"
#include "driver/NativeBuild.h"
#include "hitsimple/ast/AST.h"
#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/hir/HIR.h"
#include "hitsimple/lexer/Lexer.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/sema/Sema.h"
#include "hitsimple/stdlib/StandardLibraryModules.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"

#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace hitsimple::driver {

struct ParsedTranslationUnit {
  std::unique_ptr<hitsimple::ast::TranslationUnit> unit;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<hitsimple::stdlib::StandardHeader> standardHeaders;
};

std::string serializeLlvmModule(const llvm::Module& module) {
  std::string llvmIr;
  llvm::raw_string_ostream output(llvmIr);
  module.print(output, nullptr);
  output.flush();
  return llvmIr;
}

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
  auto emitResult = hitsimple::codegen::emitLlvmModule(
      *analyzeResult.unit, inputPath, codegenOptions);
  if (!emitResult.diagnostics.empty()) {
    printDiagnostics(emitResult.diagnostics);
    metrics.fail(unitMetrics.llvmEmission, emissionStarted);
    metrics.fail("llvm_emission");
    return std::nullopt;
  }
  metrics.complete(unitMetrics.llvmEmission, emissionStarted);

  return CompiledTranslationUnit{inputPath, std::move(emitResult), mainCount,
                                 std::move(parsed->compatibilityLinkage),
                                 sourceModules, metricsIndex};
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

std::optional<hitsimple::codegen::ModuleEmitResult> mergeLlvmModules(
    std::vector<CompiledTranslationUnit>& units, std::string& error) {
  if (units.empty()) {
    error = "no LLVM modules to merge";
    return std::nullopt;
  }
  hitsimple::codegen::ModuleEmitResult result;
  result.context = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic diagnostic;
  std::unique_ptr<llvm::Module> merged;
  for (std::size_t index = 0; index < units.size(); ++index) {
    if (!units[index].emission.module) {
      error = "internal error: missing LLVM module for '" +
          units[index].inputPath + "'";
      return std::nullopt;
    }
    auto buffer = llvm::MemoryBuffer::getMemBufferCopy(
        serializeLlvmModule(*units[index].emission.module),
        "hitsimple-module-" + std::to_string(index));
    auto module = llvm::parseAssembly(*buffer, diagnostic, *result.context);
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
  result.module = std::move(merged);
  result.nativeTarget = std::move(units.front().emission.nativeTarget);
  if (!result.nativeTarget.machine) {
    error = "internal error: missing LLVM target machine for '" +
        units.front().inputPath + "'";
    return std::nullopt;
  }
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
  for (const auto& unit : units) {
    metrics.translationUnits().at(unit.metricsIndex).llvmIrBytes =
        serializeLlvmModule(*unit.emission.module).size();
  }
  std::string mergeError;
  const auto merged = mergeLlvmModules(units, mergeError);
  if (!merged) {
    std::cerr << "hsc: cannot merge LLVM IR: " << mergeError << '\n';
    return EXIT_FAILURE;
  }

  if (!outputPath) {
    std::cout << serializeLlvmModule(*merged->module);
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
  if (!writeFile(*outputPath, serializeLlvmModule(*merged->module))) {
    std::cerr << "hsc: cannot write output file '" << *outputPath << "'\n";
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);
  metrics.markSkipped(metrics.clangBackendLink());

  return EXIT_SUCCESS;
}

std::optional<CompiledObjectTranslationUnit> compileObjectTranslationUnit(
    const std::string& inputPath, const std::filesystem::path& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions,
    bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    bool includeSourceModules,
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
  auto emitResult = hitsimple::codegen::emitLlvmModule(
      *analyzeResult.unit, inputPath, codegenOptions);
  if (!emitResult.diagnostics.empty()) {
    printDiagnostics(emitResult.diagnostics);
    metrics.fail(unitMetrics.llvmEmission, emissionStarted);
    metrics.fail("llvm_emission");
    return std::nullopt;
  }
  std::vector<CompiledTranslationUnit> units;
  units.push_back(CompiledTranslationUnit{inputPath, std::move(emitResult), mainCount,
                                          parsed->compatibilityLinkage,
                                          sourceModules, metricsIndex});
  metrics.complete(unitMetrics.llvmEmission, emissionStarted);
  std::optional<hitsimple::codegen::ModuleEmitResult> merged;
  auto* emission = &units.front().emission;
  if (includeSourceModules && !sourceModules.empty()) {
    auto modules = compileSourceModules(sourceModules, codegenOptions, metrics);
    if (!modules) {
      return std::nullopt;
    }
    units.insert(units.end(), std::make_move_iterator(modules->begin()),
                 std::make_move_iterator(modules->end()));
    std::string mergeError;
    merged = mergeLlvmModules(units, mergeError);
    if (!merged) {
      std::cerr << "hsc: cannot merge LLVM IR: " << mergeError << '\n';
      return std::nullopt;
    }
    emission = &*merged;
  }
  auto& completedUnitMetrics = metrics.translationUnits().at(metricsIndex);
  completedUnitMetrics.llvmIrBytes =
      serializeLlvmModule(*emission->module).size();
  if (!emitOptimizedObject(*emission, outputPath, backendOptions, metrics)) {
    return std::nullopt;
  }
  return CompiledObjectTranslationUnit{mainCount,
                                       std::move(parsed->compatibilityLinkage),
                                       sourceModules};
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

} // namespace hitsimple::driver
