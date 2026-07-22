#pragma once

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/codegen/OptimizationPipeline.h"
#include "hitsimple/stdlib/StandardLibraryModules.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hitsimple::driver {

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

enum class Sanitizer {
  None,
  Address,
  Undefined,
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
  Sanitizer sanitizer = Sanitizer::None;
  std::filesystem::path profilePath;
  std::optional<std::filesystem::path> optimizationRemarksPath;
};

struct DriverOptions final {
  bool showHelp = false;
  bool showVersion = false;
  bool shouldDumpTokens = false;
  bool shouldDumpAst = false;
  bool shouldDumpHir = false;
  bool shouldEmitLlvm = false;
  bool shouldPreprocessOnly = false;
  bool shouldPrintTargetInfo = false;
  bool cCompatibilityMode = false;
  bool shouldPrintTiming = false;
  DiagnosticOutputFormat diagnosticOutputFormat = DiagnosticOutputFormat::Human;
  CrateType crateType = CrateType::Bin;
  hitsimple::stdlib::BuiltinProviderSelection providerSelection =
      hitsimple::stdlib::BuiltinProviderSelection::Optimized;
  hitsimple::codegen::CodegenOptions codegenOptions;
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

  bool hasAction() const {
    return shouldDumpTokens || shouldDumpAst || shouldDumpHir ||
           shouldEmitLlvm || shouldPreprocessOnly || shouldPrintTargetInfo;
  }
};

struct DriverOptionsParseResult final {
  std::optional<DriverOptions> options;
  std::string error;
  bool shouldPrintTiming = false;
  std::optional<std::string> timingJsonPath;

  bool ok() const { return options.has_value() && error.empty(); }
};

DriverOptionsParseResult
parseDriverOptions(const std::vector<std::string>& arguments);

} // namespace hitsimple::driver
