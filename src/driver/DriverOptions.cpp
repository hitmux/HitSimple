#include "driver/DriverOptions.h"

#include "hitsimple/support/Path.h"

#include <sstream>
#include <string_view>
#include <utility>

namespace hitsimple::driver {

namespace {

DriverOptionsParseResult failDriverOptions(const DriverOptions& options,
                                           std::string error) {
  if (!error.empty() && error.back() == '\n') {
    error.pop_back();
  }
  return {std::nullopt, std::move(error), options.shouldPrintTiming,
          options.timingJsonPath};
}

} // namespace

DriverOptionsParseResult
parseDriverOptions(const std::vector<std::string>& arguments) {
  DriverOptions options;
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
  bool crateTypeSelected = false;
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
  std::ostringstream error;
  const auto fail = [&options, &error]() {
    return failDriverOptions(options, error.str());
  };

  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string_view arg(arguments[i]);

    if (arg == "-h" || arg == "--help") {
      options.showHelp = true;
      return {std::move(options), {}, false, std::nullopt};
    }

    if (arg == "--version") {
      options.showVersion = true;
      return {std::move(options), {}, false, std::nullopt};
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
        error << "--emit-object conflicts with --crate-type\n";
        return fail();
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
        error << "unsupported --crate-type '" << value
                  << "'; expected bin, object, or staticlib\n";
        return fail();
      }
      if (crateTypeSelected && crateType != selected) {
        error << "conflicting --crate-type options\n";
        return fail();
      }
      crateType = selected;
      crateTypeSelected = true;
      continue;
    }

    if (arg == "--crate-type") {
      error << "--crate-type requires --crate-type=<bin|object|staticlib>\n";
      return fail();
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
        error << "--optimization-remarks requires a file path\n";
        return fail();
      }
      if (backendOptions.optimizationRemarksPath) {
        error << "--optimization-remarks may only be specified once\n";
        return fail();
      }
      backendOptions.optimizationRemarksPath =
          hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--optimization-remarks") {
      error << "--optimization-remarks requires "
                   "--optimization-remarks=<path>\n";
      return fail();
    }

    if (arg.starts_with("--sanitize=")) {
      const auto value = arg.substr(std::string_view("--sanitize=").size());
      if (backendOptions.sanitizer != Sanitizer::None) {
        error << "--sanitize may only be specified once\n";
        return fail();
      }
      if (value == "address") {
        backendOptions.sanitizer = Sanitizer::Address;
      } else if (value == "undefined") {
        backendOptions.sanitizer = Sanitizer::Undefined;
      } else {
        error << "unsupported --sanitize '" << value
              << "'; expected address or undefined\n";
        return fail();
      }
      continue;
    }

    if (arg == "--sanitize") {
      error << "--sanitize requires --sanitize=address|undefined\n";
      return fail();
    }

    if (arg == "-g") {
      codegenOptions.emitDebugInfo = true;
      continue;
    }

    if (arg.starts_with("--pgo-instrument=")) {
      const auto path =
          arg.substr(std::string_view("--pgo-instrument=").size());
      if (path.empty()) {
        error << "--pgo-instrument requires a raw profile path\n";
        return fail();
      }
      if (backendOptions.pgoMode != PgoMode::None) {
        if (backendOptions.pgoMode == PgoMode::Instrument) {
          error << "--pgo-instrument may only be specified once\n";
        } else {
          error << "--pgo-instrument and --pgo-use are mutually exclusive\n";
        }
        return fail();
      }
      backendOptions.pgoMode = PgoMode::Instrument;
      backendOptions.profilePath = hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--pgo-instrument") {
      error << "--pgo-instrument requires --pgo-instrument=<profraw>\n";
      return fail();
    }

    if (arg.starts_with("--pgo-use=")) {
      const auto path = arg.substr(std::string_view("--pgo-use=").size());
      if (path.empty()) {
        error << "--pgo-use requires a merged profile path\n";
        return fail();
      }
      if (backendOptions.pgoMode != PgoMode::None) {
        if (backendOptions.pgoMode == PgoMode::Use) {
          error << "--pgo-use may only be specified once\n";
        } else {
          error << "--pgo-instrument and --pgo-use are mutually exclusive\n";
        }
        return fail();
      }
      backendOptions.pgoMode = PgoMode::Use;
      backendOptions.profilePath = hitsimple::support::pathFromUtf8(path);
      continue;
    }

    if (arg == "--pgo-use") {
      error << "--pgo-use requires --pgo-use=<profdata>\n";
      return fail();
    }

    if (arg == "--timing") {
      shouldPrintTiming = true;
      continue;
    }

    if (arg.starts_with("--timing-json=")) {
      const auto path = arg.substr(std::string_view("--timing-json=").size());
      if (path.empty()) {
        error << "--timing-json requires a file path\n";
        return fail();
      }
      timingJsonPath = std::string(path);
      continue;
    }

    if (arg == "--diagnostic-format=json") {
      requestedDiagnosticFormat = DiagnosticOutputFormat::Json;
      continue;
    }

    if (arg.starts_with("--diagnostic-format=")) {
      error << "unsupported --diagnostic-format '"
                << arg.substr(std::string_view("--diagnostic-format=").size())
                << "'; expected json\n";
      return fail();
    }

    if (arg == "--diagnostic-format") {
      error << "--diagnostic-format requires "
                   "--diagnostic-format=json\n";
      return fail();
    }

    if (arg == "--timing-json") {
      error << "--timing-json requires --timing-json=<path>\n";
      return fail();
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
        error << "unsupported --stdlib-provider '" << value
                  << "'; expected optimized or reference\n";
        return fail();
      }
      continue;
    }

    if (arg == "--stdlib-provider") {
      error << "--stdlib-provider requires "
                   "--stdlib-provider=optimized|reference\n";
      return fail();
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
        error << "--clang requires an executable path\n";
        return fail();
      }
      ++i;
      clangOverride = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--clangxx") {
      if (i + 1 >= arguments.size()) {
        error << "--clangxx requires an executable path\n";
        return fail();
      }
      ++i;
      clangxxOverride = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--c-source") {
      if (i + 1 >= arguments.size()) {
        error << "--c-source requires a source path\n";
        return fail();
      }
      externalInputs.cSources.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--cxx-source") {
      if (i + 1 >= arguments.size()) {
        error << "--cxx-source requires a source path\n";
        return fail();
      }
      externalInputs.cxxSources.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--link-input") {
      if (i + 1 >= arguments.size()) {
        error << "--link-input requires a path\n";
        return fail();
      }
      externalInputs.linkInputs.push_back(arguments[++i]);
      continue;
    }

    if (arg == "-L") {
      if (i + 1 >= arguments.size()) {
        error << "-L requires a directory\n";
        return fail();
      }
      externalInputs.libraryDirectories.push_back(arguments[++i]);
      continue;
    }

    if (arg == "-l") {
      if (i + 1 >= arguments.size()) {
        error << "-l requires a library name\n";
        return fail();
      }
      externalInputs.libraries.push_back(arguments[++i]);
      continue;
    }

    if (arg == "--link-arg") {
      if (i + 1 >= arguments.size()) {
        error << "--link-arg requires an argument\n";
        return fail();
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
        error << "unsupported --entry '" << value
                  << "'; expected hsc or native\n";
        return fail();
      }
      continue;
    }

    if (arg == "--entry") {
      error << "--entry requires --entry=hsc|native\n";
      return fail();
    }

    if (arg.starts_with("--linker-language=")) {
      const auto value =
          arg.substr(std::string_view("--linker-language=").size());
      if (value == "c") {
        externalInputs.linkerLanguage = LinkerLanguage::C;
      } else if (value == "cxx") {
        externalInputs.linkerLanguage = LinkerLanguage::Cxx;
      } else {
        error << "unsupported --linker-language '" << value
                  << "'; expected c or cxx\n";
        return fail();
      }
      continue;
    }

    if (arg == "--linker-language") {
      error << "--linker-language requires "
                   "--linker-language=c|cxx\n";
      return fail();
    }

    if (arg == "--cargo-manifest") {
      if (i + 1 >= arguments.size()) {
        error << "--cargo-manifest requires a Cargo.toml path\n";
        return fail();
      }
      ++i;
      cargoManifest = hitsimple::support::pathFromUtf8(arguments[i]);
      continue;
    }

    if (arg == "--cargo-package") {
      if (i + 1 >= arguments.size()) {
        error << "--cargo-package requires a package name\n";
        return fail();
      }
      cargoPackage = arguments[++i];
      continue;
    }

    if (arg == "--cargo-profile") {
      if (i + 1 >= arguments.size()) {
        error << "--cargo-profile requires a profile name\n";
        return fail();
      }
      cargoProfile = arguments[++i];
      continue;
    }

    if (arg == "--cargo-features") {
      if (i + 1 >= arguments.size()) {
        error << "--cargo-features requires a feature list\n";
        return fail();
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
        error << "-o requires an output path\n";
        return fail();
      }
      ++i;
      outputPath = arguments[i];
      continue;
    }

    if (!arg.empty() && arg.front() == '-') {
      error << "unknown option '" << arg << "'\n";
      return fail();
    }

    inputPaths.push_back(std::string(arg));
  }

  std::vector<std::string_view> actions;
  if (shouldDumpTokens) {
    if (cCompatibilityMode) {
      error << "--dump-tokens is not supported with --c-compat\n";
      return fail();
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
      error << "--c-compat is not supported with --target-info\n";
      return fail();
    }
    actions.push_back("--target-info");
  }
  if (actions.size() > 1) {
    error << "multiple action options are not allowed:";
    for (const auto action : actions) {
      error << ' ' << action;
    }
    error << '\n';
    return fail();
  }

  if (backendOptions.pgoMode != PgoMode::None &&
      crateType != CrateType::Bin) {
    error << "PGO options are only supported for --crate-type=bin\n";
    return fail();
  }
  if (backendOptions.pgoMode != PgoMode::None && !actions.empty()) {
    if (shouldEmitLlvm) {
      error << "PGO options are not supported with --emit-llvm\n";
    } else {
      error << "PGO options are only supported for executable builds\n";
    }
    return fail();
  }
  if (backendOptions.optimizationRemarksPath && !actions.empty()) {
    error << "--optimization-remarks is only supported for native "
                 "code generation\n";
    return fail();
  }
  if (backendOptions.sanitizer != Sanitizer::None &&
      crateType != CrateType::Bin) {
    error << "--sanitize is only supported for --crate-type=bin\n";
    return fail();
  }
  if (backendOptions.sanitizer != Sanitizer::None && !actions.empty()) {
    error << "--sanitize is only supported for executable builds\n";
    return fail();
  }
  const bool hasCargoBuildOptions = cargoManifest.has_value() ||
      cargoPackage.has_value() || cargoProfile.has_value() ||
      cargoFeatures.has_value() || cargoNoDefaultFeatures;
  if (hasCargoBuildOptions && !cargoManifest) {
    error << "Cargo options require --cargo-manifest <Cargo.toml>\n";
    return fail();
  }

  const bool hasMixedBuildOptions = externalInputs.hasMixedBuildOptions() ||
      clangxxOverride.has_value() || hasCargoBuildOptions;
  if (hasMixedBuildOptions && !actions.empty()) {
    error << "C/C++ source, Cargo, and native linker options are only "
                 "supported for executable builds\n";
    return fail();
  }

  if (crateType != CrateType::Bin && !actions.empty()) {
    error << "--crate-type is not supported with " << actions.front()
              << '\n';
    return fail();
  }

  if (crateType != CrateType::Bin && hasMixedBuildOptions) {
    error << "C/C++ source, Cargo, and native linker options are not "
                 "supported with --crate-type\n";
    return fail();
  }

  if (clangxxOverride && externalInputs.cxxSources.empty() &&
      (!externalInputs.linkerLanguage ||
       *externalInputs.linkerLanguage != LinkerLanguage::Cxx)) {
    error << "--clangxx requires --cxx-source or "
                 "--linker-language=cxx\n";
    return fail();
  }

  if (codegenOptions.emitDebugInfo && !(shouldEmitLlvm || actions.empty())) {
    error << "-g is only supported for executable builds and --emit-llvm\n";
    return fail();
  }

  return {std::move(options), {}, false, std::nullopt};
}

} // namespace hitsimple::driver
